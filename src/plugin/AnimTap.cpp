#include "plugin/AnimTap.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <mach-o/dyld.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace nightcoop_mac::anim_tap
{
namespace
{
constexpr uint8_t PlayerGraphSlot = 0;
constexpr size_t MostQueuedEntries = 4096;
constexpr size_t MostFeatureFields = 48;
constexpr uint32_t MostFieldBytes = 32;
constexpr uint64_t SendEveryMs = 50;
constexpr uint64_t RefreshControllerEveryMs = 100;
constexpr uint64_t ReportEveryMs = 10000;

struct SetterSite
{
    const char* Name;
    uintptr_t Offset;
    uint8_t Bytes[24];
};

const SetterSite EventSite {"PushEvent", 0x18b5dfc, {0xff, 0x03, 0x01, 0xd1, 0xf6, 0x57, 0x01, 0xa9, 0xf4, 0x4f, 0x02, 0xa9,
                                                     0xfd, 0x7b, 0x03, 0xa9, 0xfd, 0xc3, 0x00, 0x91, 0xf4, 0x03, 0x01, 0xaa}};
const SetterSite FloatSite {"SetInputFloat", 0x18b60d8, {0xff, 0x43, 0x01, 0xd1, 0xe9, 0x23, 0x01, 0x6d, 0xf6, 0x57, 0x02, 0xa9,
                                                         0xf4, 0x4f, 0x03, 0xa9, 0xfd, 0x7b, 0x04, 0xa9, 0xfd, 0x03, 0x01, 0x91}};
const SetterSite QueueSite {"the entity event queue push", 0x1021abc, {0x28, 0x00, 0x40, 0xf9, 0xe8, 0x06, 0x00, 0xb4, 0xf8, 0x5f, 0xbc, 0xa9,
                                                                       0xf6, 0x57, 0x01, 0xa9, 0xf4, 0x4f, 0x02, 0xa9, 0xfd, 0x7b, 0x03, 0xa9}};
constexpr size_t EntityEventQueueOffset = 0xd8;
const uint8_t CommonSetterBytes[24] = {0xff, 0x43, 0x01, 0xd1, 0xf8, 0x5f, 0x01, 0xa9, 0xf6, 0x57, 0x02, 0xa9,
                                       0xf4, 0x4f, 0x03, 0xa9, 0xfd, 0x7b, 0x04, 0xa9, 0xfd, 0x03, 0x01, 0x91};

using EventFunction = uint64_t (*)(void*, uint64_t);
using FloatFunction = uint64_t (*)(void*, uint64_t, float);
using IntFunction = uint64_t (*)(void*, uint64_t, int32_t);
using BoolFunction = uint64_t (*)(void*, uint64_t, bool);
using ValuesFunction = uint64_t (*)(void*, uint64_t, const float*);
using FeatureFunction = uint64_t (*)(void*, uint64_t, void*);
using QueueFunction = uint64_t (*)(void*, void*);

EventFunction OriginalPushEvent = nullptr;
FloatFunction OriginalSetFloat = nullptr;
IntFunction OriginalSetInt = nullptr;
BoolFunction OriginalSetBool = nullptr;
ValuesFunction OriginalSetVector = nullptr;
ValuesFunction OriginalSetQuaternion = nullptr;
FeatureFunction OriginalApplyFeature = nullptr;
QueueFunction OriginalQueuePush = nullptr;

LogFunction Log;
bool Installed = false;
std::atomic<void*> PlayerController {nullptr};
std::atomic<void*> PlayerEventQueue {nullptr};
RED4ext::CClass* InputSetterClass = nullptr;
RED4ext::CClass* ExternalEventClass = nullptr;
RED4ext::CProperty* SetterKey = nullptr;
RED4ext::CProperty* ExternalEventName = nullptr;
std::atomic<bool> EventTypesReady {false};
RED4ext::Handle<RED4ext::IScriptable> PlayerControllerHandle;
RED4ext::CBaseFunction* GetPlayerFunction = nullptr;
RED4ext::CBaseFunction* GetControllerFunction = nullptr;
RED4ext::CClass* ScriptedPuppetClass = nullptr;
RED4ext::CClass* AnimFeatureClass = nullptr;

std::mutex QueueLock;
std::vector<net::AnimEntry> Queue;
std::map<std::pair<uint8_t, uint64_t>, net::AnimEntry> LatestByInput;
std::map<std::pair<uint8_t, uint64_t>, net::AnimEntry> LastQueuedByInput;
uint32_t Dropped = 0;
uint32_t RecordedSinceReport = 0;
uint32_t UnchangedSinceReport = 0;
uint32_t BatchesSinceReport = 0;
uint64_t NextSendMs = 0;
uint64_t NextRefreshMs = 0;
uint64_t NextReportMs = 0;

bool SameValue(const net::AnimEntry& first, const net::AnimEntry& second)
{
    if (std::memcmp(first.Values, second.Values, sizeof(first.Values)) != 0 || first.IntValue != second.IntValue ||
        first.FeatureType != second.FeatureType || first.Fields.size() != second.Fields.size())
    {
        return false;
    }
    for (size_t index = 0; index < first.Fields.size(); ++index)
    {
        if (first.Fields[index].Name != second.Fields[index].Name || first.Fields[index].Bytes != second.Fields[index].Bytes)
        {
            return false;
        }
    }
    return true;
}

void Keep(net::AnimEntry&& entry)
{
    std::lock_guard guard(QueueLock);
    const std::pair<uint8_t, uint64_t> input {static_cast<uint8_t>(entry.Kind), entry.Name};
    LatestByInput[input] = entry;
    ++RecordedSinceReport;
    const bool isEvent = entry.Kind == net::AnimKind::Event;
    if (!isEvent)
    {
        const auto lastQueued = LastQueuedByInput.find(input);
        if (lastQueued != LastQueuedByInput.end() && SameValue(lastQueued->second, entry))
        {
            ++UnchangedSinceReport;
            return;
        }
    }
    if (Queue.size() >= MostQueuedEntries)
    {
        ++Dropped;
        LastQueuedByInput.erase(input);
        return;
    }
    if (!isEvent)
    {
        LastQueuedByInput[input] = entry;
    }
    Queue.push_back(std::move(entry));
}

bool IsOurs(void* component)
{
    return component && component == PlayerController.load(std::memory_order_acquire);
}

net::AnimEntry EntryFor(net::AnimKind kind, uint64_t name)
{
    net::AnimEntry entry;
    entry.Slot = PlayerGraphSlot;
    entry.Kind = kind;
    entry.Name = name;
    return entry;
}

uint64_t OnPushEvent(void* component, uint64_t name)
{
    if (IsOurs(component))
    {
        Keep(EntryFor(net::AnimKind::Event, name));
    }
    return OriginalPushEvent(component, name);
}

uint64_t OnSetFloat(void* component, uint64_t name, float value)
{
    if (IsOurs(component))
    {
        auto entry = EntryFor(net::AnimKind::Float, name);
        entry.Values[0] = value;
        Keep(std::move(entry));
    }
    return OriginalSetFloat(component, name, value);
}

uint64_t OnSetInt(void* component, uint64_t name, int32_t value)
{
    if (IsOurs(component))
    {
        auto entry = EntryFor(net::AnimKind::Int, name);
        entry.IntValue = value;
        Keep(std::move(entry));
    }
    return OriginalSetInt(component, name, value);
}

uint64_t OnSetBool(void* component, uint64_t name, bool value)
{
    if (IsOurs(component))
    {
        auto entry = EntryFor(net::AnimKind::Bool, name);
        entry.IntValue = value ? 1 : 0;
        Keep(std::move(entry));
    }
    return OriginalSetBool(component, name, value);
}

void KeepFourValues(net::AnimKind kind, uint64_t name, const float* values)
{
    auto entry = EntryFor(kind, name);
    if (values)
    {
        std::memcpy(entry.Values, values, sizeof(entry.Values));
    }
    Keep(std::move(entry));
}

uint64_t OnSetVector(void* component, uint64_t name, const float* values)
{
    if (IsOurs(component))
    {
        KeepFourValues(net::AnimKind::Vector, name, values);
    }
    return OriginalSetVector(component, name, values);
}

uint64_t OnSetQuaternion(void* component, uint64_t name, const float* values)
{
    if (IsOurs(component))
    {
        KeepFourValues(net::AnimKind::Quaternion, name, values);
    }
    return OriginalSetQuaternion(component, name, values);
}

void KeepFeature(uint64_t name, void* featureHandle)
{
    auto* handle = static_cast<RED4ext::Handle<RED4ext::IScriptable>*>(featureHandle);
    if (!handle || !handle->instance || !AnimFeatureClass)
    {
        return;
    }
    auto* featureClass = handle->instance->GetType();
    if (!featureClass || !featureClass->IsA(AnimFeatureClass))
    {
        return;
    }
    auto entry = EntryFor(net::AnimKind::Feature, name);
    entry.FeatureType = featureClass->GetName().hash;
    for (auto* classType = featureClass; classType && entry.Fields.size() < MostFeatureFields; classType = classType->parent)
    {
        for (uint32_t index = 0; index < classType->props.Size() && entry.Fields.size() < MostFeatureFields; ++index)
        {
            auto* property = classType->props[index];
            if (!property || !property->type)
            {
                continue;
            }
            const uint32_t size = property->type->GetSize();
            const auto type = property->type->GetType();
            if (size == 0 || size > MostFieldBytes || type == RED4ext::rtti::ERTTIType::Handle || type == RED4ext::rtti::ERTTIType::WeakHandle ||
                type == RED4ext::rtti::ERTTIType::Array)
            {
                continue;
            }
            net::AnimFeatureField field;
            field.Name = property->name.hash;
            const auto* bytes = property->GetValuePtr<uint8_t>(handle->instance);
            field.Bytes.assign(bytes, bytes + size);
            entry.Fields.push_back(std::move(field));
        }
    }
    Keep(std::move(entry));
}

uint64_t OnApplyFeature(void* component, uint64_t name, void* featureHandle)
{
    if (IsOurs(component))
    {
        KeepFeature(name, featureHandle);
    }
    return OriginalApplyFeature(component, name, featureHandle);
}

void KeepQueuedEvent(void* eventHandle)
{
    auto* handle = static_cast<RED4ext::Handle<RED4ext::IScriptable>*>(eventHandle);
    if (!handle || !handle->instance || !EventTypesReady.load(std::memory_order_acquire))
    {
        return;
    }
    auto* eventClass = handle->instance->GetType();
    if (!eventClass)
    {
        return;
    }
    if (ExternalEventClass && eventClass->IsA(ExternalEventClass))
    {
        Keep(EntryFor(net::AnimKind::Event, ExternalEventName->GetValuePtr<RED4ext::CName>(handle->instance)->hash));
        return;
    }
    if (!InputSetterClass || !eventClass->IsA(InputSetterClass))
    {
        return;
    }
    auto* value = eventClass->GetProperty("value");
    if (!value || !value->type)
    {
        return;
    }
    const uint64_t name = SetterKey->GetValuePtr<RED4ext::CName>(handle->instance)->hash;
    const std::string valueType = value->type->GetName().ToString();
    auto* valuePointer = value->GetValuePtr<uint8_t>(handle->instance);
    if (valueType == "Float")
    {
        auto entry = EntryFor(net::AnimKind::Float, name);
        std::memcpy(&entry.Values[0], valuePointer, sizeof(float));
        Keep(std::move(entry));
    }
    else if (valueType == "Int32")
    {
        auto entry = EntryFor(net::AnimKind::Int, name);
        std::memcpy(&entry.IntValue, valuePointer, sizeof(int32_t));
        Keep(std::move(entry));
    }
    else if (valueType == "Bool")
    {
        auto entry = EntryFor(net::AnimKind::Bool, name);
        entry.IntValue = *valuePointer ? 1 : 0;
        Keep(std::move(entry));
    }
    else if (valueType == "Vector4")
    {
        KeepFourValues(net::AnimKind::Vector, name, reinterpret_cast<const float*>(valuePointer));
    }
    else if (valueType == "Quaternion")
    {
        KeepFourValues(net::AnimKind::Quaternion, name, reinterpret_cast<const float*>(valuePointer));
    }
    else if (valueType == "handle:animAnimFeature")
    {
        KeepFeature(name, valuePointer);
    }
}

uint64_t OnQueuePush(void* queue, void* eventHandle)
{
    if (queue && queue == PlayerEventQueue.load(std::memory_order_acquire))
    {
        KeepQueuedEvent(eventHandle);
    }
    return OriginalQueuePush(queue, eventHandle);
}

uintptr_t GameImageBase()
{
    for (uint32_t index = 0; index < _dyld_image_count(); ++index)
    {
        const char* name = _dyld_get_image_name(index);
        if (name && std::strstr(name, "/Contents/MacOS/Cyberpunk2077"))
        {
            return reinterpret_cast<uintptr_t>(_dyld_get_image_header(index));
        }
    }
    return 0;
}

bool AttachChecked(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, uintptr_t base, const char* name, uintptr_t offset,
                   const uint8_t (&expected)[24], void* detour, void** original)
{
    const auto target = reinterpret_cast<void*>(base + offset);
    if (std::memcmp(target, expected, sizeof(expected)) != 0)
    {
        Log(std::string("anim tap: ") + name + " is not the code seen in mac 2.5.0, our anim stream stays off");
        return false;
    }
    if (!sdk->hooking->Attach(pluginHandle, target, detour, original) || !*original)
    {
        Log(std::string("anim tap: could not hook ") + name + ", our anim stream stays off");
        return false;
    }
    return true;
}

void RefreshController()
{
    if (!GetPlayerFunction)
    {
        GetPlayerFunction = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
        GetControllerFunction = game::FindFunction("ScriptedPuppet", "GetAnimationControllerComponent");
        ScriptedPuppetClass = RED4ext::CRTTISystem::Get()->GetClass("ScriptedPuppet");
        AnimFeatureClass = RED4ext::CRTTISystem::Get()->GetClass("animAnimFeature");
        auto* rtti = RED4ext::CRTTISystem::Get();
        InputSetterClass = rtti->GetClass("entAnimInputSetter");
        ExternalEventClass = rtti->GetClass("entAnimExternalEvent");
        SetterKey = InputSetterClass ? InputSetterClass->GetProperty("key") : nullptr;
        ExternalEventName = ExternalEventClass ? ExternalEventClass->GetProperty("name") : nullptr;
        const bool ready = InputSetterClass && ExternalEventClass && SetterKey && ExternalEventName;
        EventTypesReady.store(ready, std::memory_order_release);
        Log(ready ? "anim tap: anim input events found, the scripts writes get recorded too"
                  : "anim tap: entAnimInputSetter / entAnimExternalEvent not found, only direct setter writes get recorded");
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t playerArgs;
    playerArgs.emplace_back(nullptr, &gameInstance);
    RED4ext::Handle<RED4ext::IScriptable> controller;
    RED4ext::StackArgs_t noArgs;
    if (GetPlayerFunction && GetControllerFunction && game::CallStatic(GetPlayerFunction, &player, playerArgs) && player &&
        player.instance->GetType() && player.instance->GetType()->IsA(ScriptedPuppetClass))
    {
        game::CallMethod(player.instance, GetControllerFunction, &controller, noArgs);
    }
    if (controller.instance != PlayerControllerHandle.instance)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "anim tap: our players anim controller is now %p (was %p, player %p)", static_cast<void*>(controller.instance),
                      static_cast<void*>(PlayerControllerHandle.instance), static_cast<void*>(player.instance));
        Log(line);
        PlayerController.store(controller.instance, std::memory_order_release);
        PlayerEventQueue.store(controller.instance && player.instance ? reinterpret_cast<uint8_t*>(player.instance) + EntityEventQueueOffset : nullptr,
                               std::memory_order_release);
        PlayerControllerHandle = controller;
        std::lock_guard guard(QueueLock);
        LatestByInput.clear();
        LastQueuedByInput.clear();
        Queue.clear();
    }
}
}

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log)
{
    Log = std::move(log);
    if (Installed)
    {
        return true;
    }
    const uintptr_t base = GameImageBase();
    if (!base)
    {
        Log("anim tap: no game image, our anim stream stays off");
        return false;
    }
    const bool hooked =
        AttachChecked(sdk, pluginHandle, base, EventSite.Name, EventSite.Offset, EventSite.Bytes, reinterpret_cast<void*>(&OnPushEvent),
                      reinterpret_cast<void**>(&OriginalPushEvent)) &&
        AttachChecked(sdk, pluginHandle, base, FloatSite.Name, FloatSite.Offset, FloatSite.Bytes, reinterpret_cast<void*>(&OnSetFloat),
                      reinterpret_cast<void**>(&OriginalSetFloat)) &&
        AttachChecked(sdk, pluginHandle, base, "SetInputInt", 0x18b6270, CommonSetterBytes, reinterpret_cast<void*>(&OnSetInt),
                      reinterpret_cast<void**>(&OriginalSetInt)) &&
        AttachChecked(sdk, pluginHandle, base, "SetInputBool", 0x18b6408, CommonSetterBytes, reinterpret_cast<void*>(&OnSetBool),
                      reinterpret_cast<void**>(&OriginalSetBool)) &&
        AttachChecked(sdk, pluginHandle, base, "SetInputVector", 0x18b633c, CommonSetterBytes, reinterpret_cast<void*>(&OnSetVector),
                      reinterpret_cast<void**>(&OriginalSetVector)) &&
        AttachChecked(sdk, pluginHandle, base, "SetInputQuaternion", 0x18b61a4, CommonSetterBytes, reinterpret_cast<void*>(&OnSetQuaternion),
                      reinterpret_cast<void**>(&OriginalSetQuaternion)) &&
        AttachChecked(sdk, pluginHandle, base, "ApplyFeature", 0x18b64d4, CommonSetterBytes, reinterpret_cast<void*>(&OnApplyFeature),
                      reinterpret_cast<void**>(&OriginalApplyFeature)) &&
        AttachChecked(sdk, pluginHandle, base, QueueSite.Name, QueueSite.Offset, QueueSite.Bytes, reinterpret_cast<void*>(&OnQueuePush),
                      reinterpret_cast<void**>(&OriginalQueuePush));
    Installed = hooked;
    Log(hooked ? "anim tap: the 7 anim controller setters and the entity event queue are hooked, our anim stream goes to partners"
               : "anim tap: not all setters hooked");
    return hooked;
}

void Tick(uint64_t nowMs, net::RelaySession& session)
{
    if (!Installed)
    {
        return;
    }
    if (nowMs >= NextRefreshMs)
    {
        NextRefreshMs = nowMs + RefreshControllerEveryMs;
        RefreshController();
    }
    if (nowMs < NextSendMs)
    {
        return;
    }
    NextSendMs = nowMs + SendEveryMs;
    net::AnimBatch batch;
    {
        std::lock_guard guard(QueueLock);
        batch.Entries.swap(Queue);
    }
    const auto status = session.GetStatus();
    if (!batch.Entries.empty() && status.InSession)
    {
        session.SendGameMessage(net::WriteAnimBatch(batch));
        ++BatchesSinceReport;
    }
    if (nowMs >= NextReportMs)
    {
        NextReportMs = nowMs + ReportEveryMs;
        uint32_t recorded = 0;
        uint32_t unchanged = 0;
        uint32_t dropped = 0;
        {
            std::lock_guard guard(QueueLock);
            recorded = RecordedSinceReport;
            unchanged = UnchangedSinceReport;
            dropped = Dropped;
            RecordedSinceReport = 0;
            UnchangedSinceReport = 0;
            Dropped = 0;
        }
        if (recorded || BatchesSinceReport || status.InSession)
        {
            Log("anim tap: " + std::to_string(recorded) + " input(s) of our player recorded (" + std::to_string(unchanged) + " unchanged, not sent), " +
                std::to_string(BatchesSinceReport) + " batch(es) sent, " + std::to_string(dropped) + " dropped in 10 s");
        }
        BatchesSinceReport = 0;
    }
}

void SendFullState(uint32_t peerNumber, net::RelaySession& session)
{
    if (!Installed)
    {
        return;
    }
    net::AnimBatch batch;
    batch.FullState = true;
    {
        std::lock_guard guard(QueueLock);
        for (const auto& [input, entry] : LatestByInput)
        {
            if (entry.Kind != net::AnimKind::Event)
            {
                batch.Entries.push_back(entry);
            }
        }
    }
    session.SendGameMessage(net::WriteAnimBatch(batch), static_cast<int32_t>(peerNumber));
    Log("anim tap: sent partner " + std::to_string(peerNumber) + " our full anim state (" + std::to_string(batch.Entries.size()) + " input(s))");
}

void Forget()
{
    PlayerController.store(nullptr, std::memory_order_release);
    PlayerEventQueue.store(nullptr, std::memory_order_release);
    new RED4ext::Handle<RED4ext::IScriptable>(std::move(PlayerControllerHandle));
    std::lock_guard guard(QueueLock);
    Queue.clear();
    LatestByInput.clear();
}
}

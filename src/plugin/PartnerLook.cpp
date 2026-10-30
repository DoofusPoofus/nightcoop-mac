#include "plugin/PartnerLook.hpp"

#include <mach-o/dyld.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace nightcoop_mac::partner_look
{
namespace
{
constexpr uintptr_t GetStateForGenderOffset = 0x4bc7228;
constexpr uintptr_t GetStateOffset = 0x4bc6098;
constexpr uintptr_t LookPartOffset = 0x4bacda8;
constexpr uintptr_t LookPartOwnerSlot = 0x2a8;
constexpr size_t EntityIdOffset = 0x48;
constexpr uint8_t GetStateForGenderBytes[24] = {0xf6, 0x57, 0xbd, 0xa9, 0xf4, 0x4f, 0x01, 0xa9, 0xfd, 0x7b, 0x02, 0xa9,
                                                0xfd, 0x83, 0x00, 0x91, 0xf6, 0x03, 0x01, 0xaa, 0xf5, 0x03, 0x00, 0xaa};
constexpr uint8_t GetStateBytes[24] = {0xf6, 0x57, 0xbd, 0xa9, 0xf4, 0x4f, 0x01, 0xa9, 0xfd, 0x7b, 0x02, 0xa9,
                                       0xfd, 0x83, 0x00, 0x91, 0xf4, 0x03, 0x00, 0xaa, 0xf5, 0x03, 0x08, 0xaa};
constexpr uint8_t LookPartBytes[24] = {0xff, 0xc3, 0x01, 0xd1, 0xf6, 0x57, 0x04, 0xa9, 0xf4, 0x4f, 0x05, 0xa9,
                                       0xfd, 0x7b, 0x06, 0xa9, 0xfd, 0x83, 0x01, 0x91, 0xf3, 0x03, 0x01, 0xaa};

using StateHandle = RED4ext::Handle<RED4ext::IScriptable>;
using GetStateForGenderFunction = StateHandle (*)(void* system, bool isMale);
using GetStateFunction = StateHandle (*)(void* system);
using LookPartFunction = void (*)(void* component, void* slot);
using OwnerFunction = StateHandle (*)(void* component);

GetStateForGenderFunction OriginalGetStateForGender = nullptr;
GetStateFunction OriginalGetState = nullptr;
LookPartFunction OriginalLookPart = nullptr;
LogFunction Log;
bool HooksInstalled = false;

std::atomic<uint32_t> PartnerStateCount {0};
std::atomic<uint32_t> AnsweredCount {0};
std::mutex PartnerStatesLock;
struct PartnerEntry
{
    StateHandle State;
    bool IsMale = false;
};
std::unordered_map<uint64_t, PartnerEntry> PartnerStates;
std::atomic<int> OwnGender {-1};

thread_local uint64_t LookOwnerInScope = 0;

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

uint64_t OwnerEntityOf(void* component)
{
    const auto vtable = *reinterpret_cast<uintptr_t*>(component);
    const auto ownerOf = *reinterpret_cast<OwnerFunction*>(vtable + LookPartOwnerSlot);
    const StateHandle owner = ownerOf(component);
    if (!owner)
    {
        return 0;
    }
    uint64_t entityHash = 0;
    std::memcpy(&entityHash, reinterpret_cast<uint8_t*>(owner.instance) + EntityIdOffset, sizeof(entityHash));
    return entityHash;
}

void OnLookPart(void* component, void* slot)
{
    if (PartnerStateCount.load(std::memory_order_acquire) == 0 || !component)
    {
        OriginalLookPart(component, slot);
        return;
    }
    const uint64_t outerScope = LookOwnerInScope;
    LookOwnerInScope = OwnerEntityOf(component);
    OriginalLookPart(component, slot);
    LookOwnerInScope = outerScope;
}

bool ScopedState(StateHandle& state)
{
    if (!LookOwnerInScope || PartnerStateCount.load(std::memory_order_acquire) == 0)
    {
        return false;
    }
    std::lock_guard guard(PartnerStatesLock);
    const auto partner = PartnerStates.find(LookOwnerInScope);
    if (partner == PartnerStates.end() || !partner->second.State)
    {
        return false;
    }
    state = partner->second.State;
    return true;
}

bool OnlyPartnerOfGender(bool isMale, StateHandle& state)
{
    const int ownGender = OwnGender.load(std::memory_order_acquire);
    if (LookOwnerInScope || ownGender < 0 || (ownGender == 1) == isMale || PartnerStateCount.load(std::memory_order_acquire) == 0)
    {
        return false;
    }
    std::lock_guard guard(PartnerStatesLock);
    const StateHandle* found = nullptr;
    for (const auto& [entityHash, entry] : PartnerStates)
    {
        if (entry.IsMale != isMale || !entry.State)
        {
            continue;
        }
        if (found && found->instance != entry.State.instance)
        {
            return false;
        }
        found = &entry.State;
    }
    if (!found)
    {
        return false;
    }
    state = *found;
    return true;
}

StateHandle OnGetStateForGender(void* system, bool isMale)
{
    StateHandle state;
    if (ScopedState(state) || OnlyPartnerOfGender(isMale, state))
    {
        AnsweredCount.fetch_add(1, std::memory_order_relaxed);
        return state;
    }
    return OriginalGetStateForGender(system, isMale);
}

StateHandle OnGetState(void* system)
{
    StateHandle state;
    if (ScopedState(state))
    {
        AnsweredCount.fetch_add(1, std::memory_order_relaxed);
        return state;
    }
    return OriginalGetState(system);
}

bool AttachChecked(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, uintptr_t base, uintptr_t offset,
                   const uint8_t (&expected)[24], void* detour, void** original, const char* what)
{
    const auto target = reinterpret_cast<void*>(base + offset);
    if (std::memcmp(target, expected, sizeof(expected)) != 0)
    {
        Log(std::string("look: ") + what + " is not the code seen in mac 2.5.0, partners keep our look");
        return false;
    }
    if (!sdk->hooking->Attach(pluginHandle, target, detour, original) || !*original)
    {
        Log(std::string("look: could not hook ") + what + ", partners keep our look");
        return false;
    }
    return true;
}
}

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log)
{
    Log = std::move(log);
    if (HooksInstalled)
    {
        return true;
    }
    const uintptr_t base = GameImageBase();
    if (!base)
    {
        Log("look: no game image, partners keep our look");
        return false;
    }
    if (!AttachChecked(sdk, pluginHandle, base, GetStateForGenderOffset, GetStateForGenderBytes, reinterpret_cast<void*>(&OnGetStateForGender),
                       reinterpret_cast<void**>(&OriginalGetStateForGender), "the customization systems GetState(isMale)") ||
        !AttachChecked(sdk, pluginHandle, base, GetStateOffset, GetStateBytes, reinterpret_cast<void*>(&OnGetState),
                       reinterpret_cast<void**>(&OriginalGetState), "the customization systems GetState()") ||
        !AttachChecked(sdk, pluginHandle, base, LookPartOffset, LookPartBytes, reinterpret_cast<void*>(&OnLookPart),
                       reinterpret_cast<void**>(&OriginalLookPart), "the look part function"))
    {
        return false;
    }
    HooksInstalled = true;
    Log("look: GetState(isMale), GetState() and the look part function hooked, partner bodies can wear their own look");
    return true;
}

bool IsInstalled()
{
    return HooksInstalled;
}

void SetPartnerState(uint64_t entityHash, const RED4ext::Handle<RED4ext::IScriptable>& state, bool isMale)
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates[entityHash] = {state, isMale};
    PartnerStateCount.store(static_cast<uint32_t>(PartnerStates.size()), std::memory_order_release);
}

void ForgetPartnerState(uint64_t entityHash)
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates.erase(entityHash);
    PartnerStateCount.store(static_cast<uint32_t>(PartnerStates.size()), std::memory_order_release);
}

void SetOwnGender(bool isMale)
{
    OwnGender.store(isMale ? 1 : 0, std::memory_order_release);
}

uint32_t TakeAnsweredCount()
{
    return AnsweredCount.exchange(0, std::memory_order_relaxed);
}

void ForgetAllPartnerStates()
{
    std::lock_guard guard(PartnerStatesLock);
    PartnerStates.clear();
    PartnerStateCount.store(0, std::memory_order_release);
}
}

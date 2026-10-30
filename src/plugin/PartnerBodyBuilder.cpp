#include "plugin/PartnerBodyBuilder.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_set>

namespace nightcoop_mac::partner_body_builder
{
namespace
{
constexpr uint32_t ExtractComponentsJobHash = 489494088;
constexpr size_t BuilderExtractorOffset = 0x10;
constexpr size_t BuilderTemplateOffset = 0x90;
constexpr size_t BuilderRequestOffset = 0xA0;
constexpr size_t RequestEntityIdOffset = 0x20;
constexpr size_t ExtractorResultsOffset = 0xA8;
constexpr size_t TemplateRootIndexOffset = 0x1D8;
constexpr size_t ComponentIsEnabledOffset = 0x8B;
constexpr uint32_t MostObjectsInOneTemplate = 65536;
constexpr size_t MostQueuedLogLines = 200;
constexpr uint64_t PinnedRenderPlaneTrack = 0xf6c0a5626de68fc2;
constexpr uint64_t RenderPlaneParameterNames[] = {0x500468e01dd2634d, 0x44b42b9359b79f22, 0x13e09e8b9d7d102f};

const char* KeptOffClassNames[] = {
    "gameFPPCameraComponent",
    "vehicleCameraManagerComponent",
    "vehicleTPPCameraComponent",
    "entSoundListenerComponent",
    "WidgetHudComponent",
    "WidgetMenuComponent",
    "gamestateMachineComponent",
    "gamePlayerCommandConsumerComponent",
    "vehicleGarageComponent",
    "gameComponentsStateSaveComponent",
    "gamePlayerTierComponent",
    "gamePlayerMappinComponent",
    "gameBreachFinderComponent",
    "gameTargetingActivatorComponent",
    "gameVisionActivatorComponent",
    "gameScanningActivatorComponent",
    "entTriggerActivatorComponent",
    "gameinteractionsComponent",
    "InspectionComponent",
    "DisarmComponent",
    "DEBUG_VisualizerComponent",
    "gameAudioEmitterComponent",
    "senseSensorObjectComponent",
    "WeaponPositionComponent",
    "vehicleVisualCustomizationComponent",
};

struct BuilderJobParams
{
    uint8_t* Builder;
    void* WeakBuilderInstance;
    RED4ext::RefCnt* WeakBuilderRefCount;
};

struct ExtractedObjects
{
    RED4ext::Handle<RED4ext::ISerializable>* Entries;
    uint32_t Capacity;
    uint32_t Size;
};

struct ArrayOfStructs
{
    RED4ext::CProperty* Array = nullptr;
    RED4ext::CRTTIBaseArrayType* ArrayType = nullptr;
    RED4ext::CProperty* FirstName = nullptr;
    RED4ext::CProperty* SecondName = nullptr;
    RED4ext::CProperty* Value = nullptr;
};

struct BuildCounts
{
    uint32_t KeptOff = 0;
    uint32_t NotReplaced = 0;
    uint32_t PartsShownInFpp = 0;
    uint32_t SlotOptionsOffWeaponPlane = 0;
    uint32_t RenderPlaneParamsPinned = 0;
    std::string KeptOffNames;
};

using ExtractComponentsJobFunction = void (*)(BuilderJobParams*, void*);

ExtractComponentsJobFunction OriginalExtractComponentsJob = nullptr;
LogFunction Log;
bool HookIsInstalled = false;
bool LookedUp = false;
bool ReadyForBodies = false;

RED4ext::CClass* ComponentClass = nullptr;
RED4ext::CClass* SlotComponentClass = nullptr;
RED4ext::CClass* PlayerPuppetClass = nullptr;
RED4ext::CClass* BodyPartsControllerClass = nullptr;
RED4ext::CClass* AttachmentSlotsClass = nullptr;
RED4ext::CClass* AnimatedComponentClass = nullptr;
RED4ext::CProperty* ComponentNameProperty = nullptr;
RED4ext::CProperty* ComponentIdProperty = nullptr;
RED4ext::CProperty* HiddenInFppProperty = nullptr;
ArrayOfStructs SlotAnimParams;
ArrayOfStructs AnimatedTrackParams;
std::vector<RED4ext::CClass*> KeptOffClasses;

std::atomic<uint32_t> ExpectedBodyCount {0};
std::mutex ExpectedBodiesLock;
std::unordered_set<uint64_t> ExpectedBodies;

std::mutex LogLinesLock;
std::vector<std::string> QueuedLogLines;

void QueueLine(std::string line)
{
    std::lock_guard guard(LogLinesLock);
    if (QueuedLogLines.size() < MostQueuedLogLines)
    {
        QueuedLogLines.push_back(std::move(line));
    }
}

template<typename Value>
Value ReadAt(const uint8_t* base, size_t offset)
{
    Value value {};
    std::memcpy(&value, base + offset, sizeof(Value));
    return value;
}

std::string TypeNameOf(const RED4ext::rtti::IType* type)
{
    return type ? std::string(type->GetName().ToString()) : std::string("nothing");
}

std::string NameText(RED4ext::CName name)
{
    const char* text = name.ToString();
    return text && *text ? std::string(text) : std::string("?");
}

RED4ext::CProperty* FindProperty(RED4ext::CClass* owner, const char* name, const char* typeName, uint32_t size, std::string& missing)
{
    auto* property = owner ? owner->GetProperty(name) : nullptr;
    if (!property || !property->type)
    {
        missing += std::string(missing.empty() ? "" : ", ") + name + " is missing";
        return nullptr;
    }
    const std::string actualType = TypeNameOf(property->type);
    const bool typeFits = typeName ? actualType == typeName : property->type->GetType() == RED4ext::rtti::ERTTIType::Enum;
    if (!typeFits || (size && property->type->GetSize() != size))
    {
        missing += std::string(missing.empty() ? "" : ", ") + name + " is " + actualType;
        return nullptr;
    }
    return property;
}

ArrayOfStructs FindArrayOfStructs(RED4ext::CClass* owner, const char* arrayName, const char* firstName, const char* secondName,
                                  const char* valueName, std::string& missing)
{
    ArrayOfStructs found;
    auto* property = owner ? owner->GetProperty(arrayName) : nullptr;
    if (!property || !property->type || property->type->GetType() != RED4ext::rtti::ERTTIType::Array)
    {
        missing += std::string(missing.empty() ? "" : ", ") + arrayName + " is no array";
        return {};
    }
    auto* arrayType = static_cast<RED4ext::CRTTIBaseArrayType*>(property->type);
    auto* innerType = arrayType->GetInnerType();
    if (!innerType || innerType->GetType() != RED4ext::rtti::ERTTIType::Class)
    {
        missing += std::string(missing.empty() ? "" : ", ") + arrayName + " holds " + TypeNameOf(innerType);
        return {};
    }
    auto* elementClass = static_cast<RED4ext::CClass*>(innerType);
    found.FirstName = FindProperty(elementClass, firstName, "CName", 8, missing);
    found.SecondName = secondName ? FindProperty(elementClass, secondName, "CName", 8, missing) : nullptr;
    found.Value = secondName ? FindProperty(elementClass, valueName, "Float", 4, missing) : FindProperty(elementClass, valueName, nullptr, 0, missing);
    if (!found.FirstName || !found.Value || (secondName && !found.SecondName))
    {
        return {};
    }
    found.Array = property;
    found.ArrayType = arrayType;
    return found;
}

uint64_t ReadEnumValue(RED4ext::CProperty* property, void* owner)
{
    const uint32_t size = property->type->GetSize();
    uint64_t value = 0;
    std::memcpy(&value, property->GetValuePtr<uint8_t>(owner), size <= sizeof(value) ? size : sizeof(value));
    return value;
}

uint32_t TakeSlotOptionsOffWeaponPlane(void* component)
{
    void* options = SlotAnimParams.Array->GetValuePtr<uint8_t>(component);
    const uint32_t count = SlotAnimParams.ArrayType->GetLength(options);
    uint32_t changed = 0;
    for (uint32_t index = 0; index < count; ++index)
    {
        void* option = SlotAnimParams.ArrayType->GetElement(options, index);
        if (!option)
        {
            continue;
        }
        auto* paramName = SlotAnimParams.FirstName->GetValuePtr<RED4ext::CName>(option);
        if (paramName->hash != 0 && ReadEnumValue(SlotAnimParams.Value, option) == 0)
        {
            paramName->hash = 0;
            ++changed;
        }
    }
    return changed;
}

uint32_t PinRenderPlaneParams(void* component)
{
    void* parameters = AnimatedTrackParams.Array->GetValuePtr<uint8_t>(component);
    const uint32_t count = AnimatedTrackParams.ArrayType->GetLength(parameters);
    uint32_t changed = 0;
    for (uint32_t index = 0; index < count; ++index)
    {
        void* parameter = AnimatedTrackParams.ArrayType->GetElement(parameters, index);
        if (!parameter)
        {
            continue;
        }
        const uint64_t parameterName = AnimatedTrackParams.FirstName->GetValuePtr<RED4ext::CName>(parameter)->hash;
        bool isRenderPlane = false;
        for (uint64_t renderPlaneName : RenderPlaneParameterNames)
        {
            isRenderPlane = isRenderPlane || parameterName == renderPlaneName;
        }
        if (!isRenderPlane)
        {
            continue;
        }
        AnimatedTrackParams.SecondName->GetValuePtr<RED4ext::CName>(parameter)->hash = PinnedRenderPlaneTrack;
        *AnimatedTrackParams.Value->GetValuePtr<float>(parameter) = 0.0f;
        ++changed;
    }
    return changed;
}

bool IsKeptOff(RED4ext::CClass* objectClass)
{
    for (auto* keptOffClass : KeptOffClasses)
    {
        if (objectClass->IsA(keptOffClass))
        {
            return true;
        }
    }
    return false;
}

bool SwapForEmptySlot(ExtractedObjects& objects, uint32_t index, RED4ext::ISerializable* original, BuildCounts& counts)
{
    auto* replacement = static_cast<RED4ext::ISerializable*>(SlotComponentClass->CreateInstance(true));
    if (!replacement)
    {
        return false;
    }
    const std::string originalName = NameText(*ComponentNameProperty->GetValuePtr<RED4ext::CName>(original));
    const std::string replacementName = "NightCoopRemoved_" + originalName;
    *ComponentNameProperty->GetValuePtr<RED4ext::CName>(replacement) = RED4ext::CName(replacementName.c_str());
    std::memcpy(ComponentIdProperty->GetValuePtr<uint8_t>(replacement), ComponentIdProperty->GetValuePtr<uint8_t>(original), sizeof(uint64_t));
    reinterpret_cast<uint8_t*>(replacement)[ComponentIsEnabledOffset] = 0;

    const std::string className = NameText(original->GetType()->GetName());
    objects.Entries[index] = RED4ext::Handle<RED4ext::ISerializable>(replacement);
    if (counts.KeptOffNames.size() < 900)
    {
        counts.KeptOffNames += (counts.KeptOffNames.empty() ? "" : ", ") + className + " '" + originalName + "'";
    }
    return true;
}

void ApplyPartnerFixes(void* component, RED4ext::CClass* componentClass, BuildCounts& counts)
{
    if (HiddenInFppProperty && BodyPartsControllerClass && componentClass->IsA(BodyPartsControllerClass))
    {
        auto* hidden = HiddenInFppProperty->GetValuePtr<bool>(component);
        if (*hidden)
        {
            *hidden = false;
            ++counts.PartsShownInFpp;
        }
    }
    if (SlotAnimParams.Array && AttachmentSlotsClass && componentClass->IsA(AttachmentSlotsClass))
    {
        counts.SlotOptionsOffWeaponPlane += TakeSlotOptionsOffWeaponPlane(component);
    }
    if (AnimatedTrackParams.Array && AnimatedComponentClass && componentClass->IsA(AnimatedComponentClass))
    {
        counts.RenderPlaneParamsPinned += PinRenderPlaneParams(component);
    }
}

bool IsExpected(uint64_t entityHash)
{
    std::lock_guard guard(ExpectedBodiesLock);
    return ExpectedBodies.count(entityHash) != 0;
}

void BuildIfPartnerBody(uint8_t* builder)
{
    const auto* request = ReadAt<uint8_t*>(builder, BuilderRequestOffset);
    if (!request)
    {
        return;
    }
    const uint64_t entityHash = ReadAt<uint64_t>(request, RequestEntityIdOffset);
    if (!IsExpected(entityHash))
    {
        return;
    }
    char line[1400];
    auto* extractor = ReadAt<uint8_t*>(builder, BuilderExtractorOffset);
    const auto* entityTemplate = ReadAt<uint8_t*>(builder, BuilderTemplateOffset);
    if (!extractor || !entityTemplate)
    {
        std::snprintf(line, sizeof(line), "builder: partner body %#llx is built without an extractor or a template, left as is",
                      static_cast<unsigned long long>(entityHash));
        QueueLine(line);
        return;
    }
    auto& objects = *reinterpret_cast<ExtractedObjects*>(extractor + ExtractorResultsOffset);
    const int16_t rootIndex = ReadAt<int16_t>(entityTemplate, TemplateRootIndexOffset);
    if (!objects.Entries || objects.Size > objects.Capacity || objects.Size > MostObjectsInOneTemplate || rootIndex < 0 ||
        static_cast<uint32_t>(rootIndex) >= objects.Size)
    {
        std::snprintf(line, sizeof(line), "builder: partner body %#llx has %u object(s) (capacity %u) and root %d, that looks wrong, left as is",
                      static_cast<unsigned long long>(entityHash), objects.Size, objects.Capacity, rootIndex);
        QueueLine(line);
        return;
    }
    auto* root = objects.Entries[rootIndex].instance;
    auto* rootClass = root ? root->GetType() : nullptr;
    if (!rootClass || !rootClass->IsA(PlayerPuppetClass))
    {
        std::snprintf(line, sizeof(line), "builder: partner body %#llx has a %s root, not a PlayerPuppet, left as is",
                      static_cast<unsigned long long>(entityHash), rootClass ? NameText(rootClass->GetName()).c_str() : "missing");
        QueueLine(line);
        return;
    }

    BuildCounts counts;
    for (uint32_t index = static_cast<uint32_t>(rootIndex) + 1; index < objects.Size; ++index)
    {
        auto* object = objects.Entries[index].instance;
        auto* objectClass = object ? object->GetType() : nullptr;
        if (!objectClass || !objectClass->IsA(ComponentClass))
        {
            continue;
        }
        if (IsKeptOff(objectClass))
        {
            if (SwapForEmptySlot(objects, index, object, counts))
            {
                ++counts.KeptOff;
            }
            else
            {
                reinterpret_cast<uint8_t*>(object)[ComponentIsEnabledOffset] = 0;
                ++counts.NotReplaced;
            }
            continue;
        }
        ApplyPartnerFixes(object, objectClass, counts);
    }
    std::snprintf(line, sizeof(line),
                  "builder: partner body %#llx built as a PlayerPuppet from %u object(s): %u component(s) kept off (%s), %u only turned off, "
                  "%u part(s) no longer hidden in first person, %u slot option(s) off the weapon plane, %u render plane param(s) pinned",
                  static_cast<unsigned long long>(entityHash), objects.Size - rootIndex - 1, counts.KeptOff,
                  counts.KeptOffNames.empty() ? "none" : counts.KeptOffNames.c_str(), counts.NotReplaced, counts.PartsShownInFpp,
                  counts.SlotOptionsOffWeaponPlane, counts.RenderPlaneParamsPinned);
    QueueLine(line);
}

// the whole port hangs on this bum ahh worker thread function not exploding
void OnExtractComponentsJob(BuilderJobParams* params, void* jobContext)
{
    if (ExpectedBodyCount.load(std::memory_order_acquire) != 0 && params && params->Builder && params->WeakBuilderRefCount &&
        params->WeakBuilderRefCount->strongRefs != 0)
    {
        BuildIfPartnerBody(params->Builder);
    }
    OriginalExtractComponentsJob(params, jobContext);
}
}

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log)
{
    Log = std::move(log);
    if (HookIsInstalled)
    {
        return true;
    }
    const uintptr_t target = RED4ext::UniversalRelocBase::Resolve(ExtractComponentsJobHash);
    if (!target)
    {
        Log("builder: EntityBuilder_ExtractComponentsJob is not verified in the address db, no PlayerPuppet partner bodies");
        return false;
    }
    if (!sdk->hooking->Attach(pluginHandle, reinterpret_cast<void*>(target), reinterpret_cast<void*>(&OnExtractComponentsJob),
                              reinterpret_cast<void**>(&OriginalExtractComponentsJob)) ||
        !OriginalExtractComponentsJob)
    {
        Log("builder: could not hook EntityBuilder_ExtractComponentsJob, no PlayerPuppet partner bodies");
        return false;
    }
    HookIsInstalled = true;
    Log("builder: entity build job hooked, PlayerPuppet partner bodies get built like nightcoops");
    return true;
}

bool PrepareForBodies()
{
    if (LookedUp)
    {
        return ReadyForBodies;
    }
    LookedUp = true;
    if (!HookIsInstalled)
    {
        return false;
    }
    auto* rtti = RED4ext::CRTTISystem::Get();
    ComponentClass = rtti->GetClass("entIComponent");
    SlotComponentClass = rtti->GetClass("entSlotComponent");
    PlayerPuppetClass = rtti->GetClass("PlayerPuppet");
    BodyPartsControllerClass = rtti->GetClass("gameuiCharacterCustomizationBodyPartsController");
    AttachmentSlotsClass = rtti->GetClass("gameAttachmentSlots");
    AnimatedComponentClass = rtti->GetClass("entAnimatedComponent");

    std::string missing;
    ComponentNameProperty = FindProperty(ComponentClass, "name", "CName", 8, missing);
    ComponentIdProperty = FindProperty(ComponentClass, "id", "CRUID", 8, missing);
    if (!ComponentClass || !SlotComponentClass || !PlayerPuppetClass || !ComponentNameProperty || !ComponentIdProperty)
    {
        Log("builder: the component classes look different than expected (" + missing + "), no PlayerPuppet partner bodies");
        return false;
    }
    std::string notFound;
    for (const char* className : KeptOffClassNames)
    {
        if (auto* keptOffClass = rtti->GetClass(className))
        {
            KeptOffClasses.push_back(keptOffClass);
        }
        else
        {
            notFound += std::string(notFound.empty() ? "" : ", ") + className;
        }
    }
    if (!notFound.empty())
    {
        Log("builder: no class to keep off the partner body for " + notFound);
    }

    std::string fixesMissing;
    HiddenInFppProperty = FindProperty(BodyPartsControllerClass, "isHiddenInFpp", "Bool", 1, fixesMissing);
    SlotAnimParams = FindArrayOfStructs(AttachmentSlotsClass, "animParams", "paramName", nullptr, "function", fixesMissing);
    AnimatedTrackParams = FindArrayOfStructs(AnimatedComponentClass, "animParameters", "parameterName", "animTrackName", "defaultValue", fixesMissing);
    if (!fixesMissing.empty())
    {
        Log("builder: some first person fixes are off (" + fixesMissing + ")");
    }
    ReadyForBodies = true;
    Log("builder: ready, " + std::to_string(KeptOffClasses.size()) + " component class(es) get kept off partner bodies");
    return true;
}

void ExpectBody(uint64_t entityHash)
{
    std::lock_guard guard(ExpectedBodiesLock);
    if (ExpectedBodies.insert(entityHash).second)
    {
        ExpectedBodyCount.fetch_add(1, std::memory_order_release);
    }
}

void ForgetBody(uint64_t entityHash)
{
    std::lock_guard guard(ExpectedBodiesLock);
    if (ExpectedBodies.erase(entityHash))
    {
        ExpectedBodyCount.fetch_sub(1, std::memory_order_release);
    }
}

void ForgetAllBodies()
{
    std::lock_guard guard(ExpectedBodiesLock);
    ExpectedBodies.clear();
    ExpectedBodyCount.store(0, std::memory_order_release);
}

std::vector<std::string> TakeLogLines()
{
    std::lock_guard guard(LogLinesLock);
    std::vector<std::string> lines;
    lines.swap(QueuedLogLines);
    return lines;
}
}

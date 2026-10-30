#include "plugin/LookStates.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <cstring>

namespace nightcoop_mac::look_states
{
namespace
{
constexpr size_t BuiltFlagOffset = 0x41;
constexpr size_t BodyMaleOffset = 0x44;
constexpr size_t BrainMaleOffset = 0x45;
constexpr size_t DataVersionOffset = 0xC0;
constexpr size_t GroupListOffsets[3] = {0x70, 0x80, 0x90};
constexpr size_t PerspectivesOffset = 0xA0;
constexpr size_t TagsOffset = 0xB0;

struct LookTypes
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CClass* StateClass = nullptr;
    RED4ext::CBaseFunction* GetSystem = nullptr;
    RED4ext::CBaseFunction* GetState = nullptr;
    RED4ext::CRTTIBaseArrayType* GroupArray = nullptr;
    RED4ext::CRTTIBaseArrayType* PartArray = nullptr;
    RED4ext::CRTTIBaseArrayType* MorphArray = nullptr;
    RED4ext::CRTTIBaseArrayType* PerspectiveArray = nullptr;
    RED4ext::CRTTIBaseArrayType* TagArray = nullptr;
    RED4ext::CProperty* GroupName = nullptr;
    RED4ext::CProperty* GroupParts = nullptr;
    RED4ext::CProperty* GroupMorphs = nullptr;
    RED4ext::CProperty* PartName = nullptr;
    RED4ext::CProperty* PartResource = nullptr;
    RED4ext::CProperty* PartDefinition = nullptr;
    RED4ext::CProperty* PartCensorFlag = nullptr;
    RED4ext::CProperty* PartCensorAction = nullptr;
    RED4ext::CProperty* MorphRegion = nullptr;
    RED4ext::CProperty* MorphTarget = nullptr;
    RED4ext::CProperty* MorphCensorFlag = nullptr;
    RED4ext::CProperty* MorphCensorAction = nullptr;
    RED4ext::CProperty* PerspectiveName = nullptr;
    RED4ext::CProperty* PerspectiveFpp = nullptr;
    RED4ext::CProperty* PerspectiveTpp = nullptr;
};

LookTypes Types;

RED4ext::CProperty* FindPropertyDeep(RED4ext::CClass* owner, const char* name)
{
    const RED4ext::CName wanted(name);
    for (auto* classType = owner; classType; classType = classType->parent)
    {
        for (uint32_t index = 0; index < classType->props.Size(); ++index)
        {
            if (classType->props[index]->name == wanted)
            {
                return classType->props[index];
            }
        }
    }
    return nullptr;
}

RED4ext::CRTTIBaseArrayType* ArrayTypeOf(RED4ext::CProperty* property)
{
    if (!property || !property->type || property->type->GetType() != RED4ext::rtti::ERTTIType::Array)
    {
        return nullptr;
    }
    return static_cast<RED4ext::CRTTIBaseArrayType*>(property->type);
}

RED4ext::CClass* ElementClassOf(RED4ext::CRTTIBaseArrayType* arrayType)
{
    auto* inner = arrayType ? arrayType->GetInnerType() : nullptr;
    return inner && inner->GetType() == RED4ext::rtti::ERTTIType::Class ? static_cast<RED4ext::CClass*>(inner) : nullptr;
}

bool Resolve(const LogFunction& log)
{
    if (Types.Resolved)
    {
        return Types.Usable;
    }
    Types.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Types.StateClass = rtti->GetClass("gameuiCharacterCustomizationState");
    Types.GetSystem = game::FindFunction("ScriptGameInstance", "GetCharacterCustomizationSystem");
    Types.GetState = game::FindFunction("gameuiICharacterCustomizationSystem", "GetState");
    auto* presetClass = rtti->GetClass("gameuiCharacterCustomizationPreset");
    auto* tagListClass = rtti->GetClass("redTagList");
    Types.GroupArray = ArrayTypeOf(presetClass ? FindPropertyDeep(presetClass, "headGroups") : nullptr);
    Types.PerspectiveArray = ArrayTypeOf(presetClass ? FindPropertyDeep(presetClass, "perspectiveInfo") : nullptr);
    Types.TagArray = ArrayTypeOf(tagListClass ? FindPropertyDeep(tagListClass, "tags") : nullptr);
    auto* groupClass = ElementClassOf(Types.GroupArray);
    Types.GroupName = groupClass ? FindPropertyDeep(groupClass, "name") : nullptr;
    Types.GroupParts = groupClass ? FindPropertyDeep(groupClass, "customization") : nullptr;
    Types.GroupMorphs = groupClass ? FindPropertyDeep(groupClass, "morphs") : nullptr;
    Types.PartArray = ArrayTypeOf(Types.GroupParts);
    Types.MorphArray = ArrayTypeOf(Types.GroupMorphs);
    auto* partClass = ElementClassOf(Types.PartArray);
    auto* morphClass = ElementClassOf(Types.MorphArray);
    auto* perspectiveClass = ElementClassOf(Types.PerspectiveArray);
    if (partClass)
    {
        Types.PartName = FindPropertyDeep(partClass, "name");
        Types.PartResource = FindPropertyDeep(partClass, "resource");
        Types.PartDefinition = FindPropertyDeep(partClass, "definition");
        Types.PartCensorFlag = FindPropertyDeep(partClass, "censorFlag");
        Types.PartCensorAction = FindPropertyDeep(partClass, "censorFlagAction");
    }
    if (morphClass)
    {
        Types.MorphRegion = FindPropertyDeep(morphClass, "regionName");
        Types.MorphTarget = FindPropertyDeep(morphClass, "targetName");
        Types.MorphCensorFlag = FindPropertyDeep(morphClass, "censorFlag");
        Types.MorphCensorAction = FindPropertyDeep(morphClass, "censorFlagAction");
    }
    if (perspectiveClass)
    {
        Types.PerspectiveName = FindPropertyDeep(perspectiveClass, "name");
        Types.PerspectiveFpp = FindPropertyDeep(perspectiveClass, "fpp");
        Types.PerspectiveTpp = FindPropertyDeep(perspectiveClass, "tpp");
    }
    Types.Usable = Types.StateClass && Types.StateClass->GetSize() > TagsOffset + 0x10 && game::ParamsAre(Types.GetSystem, {"ScriptGameInstance"}) &&
                   game::ParamsAre(Types.GetState, {}) && Types.GroupArray && Types.PartArray && Types.MorphArray && Types.PerspectiveArray &&
                   Types.TagArray && Types.GroupName && Types.PartName && Types.PartResource && Types.PartDefinition && Types.MorphRegion &&
                   Types.MorphTarget && Types.PerspectiveName && Types.PerspectiveFpp && Types.PerspectiveTpp;
    log(Types.Usable ? "looks: look state types found, looks can be read and built"
                     : "looks: the look state types are not what nightcoop expects, no partner looks");
    return Types.Usable;
}

uint64_t ReadValue(RED4ext::CProperty* property, void* owner)
{
    uint64_t value = 0;
    if (property && property->type)
    {
        const uint32_t size = property->type->GetSize();
        std::memcpy(&value, property->GetValuePtr<uint8_t>(owner), size < sizeof(value) ? size : sizeof(value));
    }
    return value;
}

void WriteValue(RED4ext::CProperty* property, void* owner, uint64_t value)
{
    if (property && property->type)
    {
        const uint32_t size = property->type->GetSize();
        std::memcpy(property->GetValuePtr<uint8_t>(owner), &value, size < sizeof(value) ? size : sizeof(value));
    }
}

bool SizeArray(RED4ext::CRTTIBaseArrayType* arrayType, void* array, size_t wanted)
{
    if (arrayType->GetLength(array) != wanted)
    {
        arrayType->Resize(array, static_cast<uint32_t>(wanted));
    }
    return arrayType->GetLength(array) == wanted;
}

void* FieldOf(RED4ext::IScriptable* state, size_t offset)
{
    return reinterpret_cast<uint8_t*>(state) + offset;
}

void ReadGroups(void* groupList, std::vector<net::LookGroup>& groups)
{
    const uint32_t groupCount = Types.GroupArray->GetLength(groupList);
    for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
    {
        void* sourceGroup = Types.GroupArray->GetElement(groupList, groupIndex);
        net::LookGroup group;
        group.Name = ReadValue(Types.GroupName, sourceGroup);
        void* parts = Types.GroupParts->GetValuePtr<uint8_t>(sourceGroup);
        for (uint32_t partIndex = 0; partIndex < Types.PartArray->GetLength(parts); ++partIndex)
        {
            void* sourcePart = Types.PartArray->GetElement(parts, partIndex);
            group.Parts.push_back({ReadValue(Types.PartResource, sourcePart), ReadValue(Types.PartDefinition, sourcePart),
                                   ReadValue(Types.PartName, sourcePart), static_cast<uint32_t>(ReadValue(Types.PartCensorFlag, sourcePart)),
                                   static_cast<uint8_t>(ReadValue(Types.PartCensorAction, sourcePart))});
        }
        void* morphs = Types.GroupMorphs->GetValuePtr<uint8_t>(sourceGroup);
        for (uint32_t morphIndex = 0; morphIndex < Types.MorphArray->GetLength(morphs); ++morphIndex)
        {
            void* sourceMorph = Types.MorphArray->GetElement(morphs, morphIndex);
            group.Morphs.push_back({ReadValue(Types.MorphRegion, sourceMorph), ReadValue(Types.MorphTarget, sourceMorph),
                                    static_cast<uint32_t>(ReadValue(Types.MorphCensorFlag, sourceMorph)),
                                    static_cast<uint8_t>(ReadValue(Types.MorphCensorAction, sourceMorph))});
        }
        groups.push_back(std::move(group));
    }
}

bool WriteGroups(void* groupList, const std::vector<net::LookGroup>& groups)
{
    if (!SizeArray(Types.GroupArray, groupList, groups.size()))
    {
        return false;
    }
    for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const auto& group = groups[groupIndex];
        void* targetGroup = Types.GroupArray->GetElement(groupList, groupIndex);
        WriteValue(Types.GroupName, targetGroup, group.Name);
        void* parts = Types.GroupParts->GetValuePtr<uint8_t>(targetGroup);
        void* morphs = Types.GroupMorphs->GetValuePtr<uint8_t>(targetGroup);
        if (!SizeArray(Types.PartArray, parts, group.Parts.size()) || !SizeArray(Types.MorphArray, morphs, group.Morphs.size()))
        {
            return false;
        }
        for (uint32_t partIndex = 0; partIndex < group.Parts.size(); ++partIndex)
        {
            void* targetPart = Types.PartArray->GetElement(parts, partIndex);
            const auto& part = group.Parts[partIndex];
            WriteValue(Types.PartResource, targetPart, part.Resource);
            WriteValue(Types.PartDefinition, targetPart, part.Definition);
            WriteValue(Types.PartName, targetPart, part.Name);
            WriteValue(Types.PartCensorFlag, targetPart, part.CensorFlag);
            WriteValue(Types.PartCensorAction, targetPart, part.CensorAction);
        }
        for (uint32_t morphIndex = 0; morphIndex < group.Morphs.size(); ++morphIndex)
        {
            void* targetMorph = Types.MorphArray->GetElement(morphs, morphIndex);
            const auto& morph = group.Morphs[morphIndex];
            WriteValue(Types.MorphRegion, targetMorph, morph.RegionName);
            WriteValue(Types.MorphTarget, targetMorph, morph.TargetName);
            WriteValue(Types.MorphCensorFlag, targetMorph, morph.CensorFlag);
            WriteValue(Types.MorphCensorAction, targetMorph, morph.CensorAction);
        }
    }
    return true;
}
}

bool ReadOwnLook(net::PlayerCustomization& look, const LogFunction& log)
{
    if (!Resolve(log))
    {
        return false;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> system;
    RED4ext::StackArgs_t systemArgs;
    systemArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(Types.GetSystem, &system, systemArgs) || !system)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> state;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(system.instance, Types.GetState, &state, noArgs) || !state || !state.instance->GetType() ||
        !state.instance->GetType()->IsA(Types.StateClass) || !*static_cast<uint8_t*>(FieldOf(state.instance, BuiltFlagOffset)))
    {
        return false;
    }
    look = net::PlayerCustomization {};
    std::memcpy(&look.DataVersion, FieldOf(state.instance, DataVersionOffset), sizeof(look.DataVersion));
    look.BodyMale = *static_cast<uint8_t*>(FieldOf(state.instance, BodyMaleOffset)) != 0;
    look.BrainMale = *static_cast<uint8_t*>(FieldOf(state.instance, BrainMaleOffset)) != 0;
    ReadGroups(FieldOf(state.instance, GroupListOffsets[0]), look.Head);
    ReadGroups(FieldOf(state.instance, GroupListOffsets[1]), look.Body);
    ReadGroups(FieldOf(state.instance, GroupListOffsets[2]), look.Arms);
    void* perspectives = FieldOf(state.instance, PerspectivesOffset);
    for (uint32_t index = 0; index < Types.PerspectiveArray->GetLength(perspectives); ++index)
    {
        void* perspective = Types.PerspectiveArray->GetElement(perspectives, index);
        look.Perspectives.push_back({ReadValue(Types.PerspectiveName, perspective), ReadValue(Types.PerspectiveFpp, perspective),
                                     ReadValue(Types.PerspectiveTpp, perspective)});
    }
    void* tags = FieldOf(state.instance, TagsOffset);
    for (uint32_t index = 0; index < Types.TagArray->GetLength(tags); ++index)
    {
        uint64_t tag = 0;
        std::memcpy(&tag, Types.TagArray->GetElement(tags, index), sizeof(tag));
        look.Tags.push_back(tag);
    }
    return true;
}

RED4ext::Handle<RED4ext::IScriptable> BuildState(const net::PlayerCustomization& look, const LogFunction& log)
{
    if (!Resolve(log))
    {
        return {};
    }
    auto* state = static_cast<RED4ext::IScriptable*>(Types.StateClass->CreateInstance(true));
    if (!state)
    {
        return {};
    }
    RED4ext::Handle<RED4ext::IScriptable> stateHandle(state);
    std::memcpy(FieldOf(state, DataVersionOffset), &look.DataVersion, sizeof(look.DataVersion));
    *static_cast<uint8_t*>(FieldOf(state, BodyMaleOffset)) = look.BodyMale ? 1 : 0;
    *static_cast<uint8_t*>(FieldOf(state, BrainMaleOffset)) = look.BrainMale ? 1 : 0;
    const bool groupsWritten = WriteGroups(FieldOf(state, GroupListOffsets[0]), look.Head) &&
                               WriteGroups(FieldOf(state, GroupListOffsets[1]), look.Body) &&
                               WriteGroups(FieldOf(state, GroupListOffsets[2]), look.Arms);
    void* perspectives = FieldOf(state, PerspectivesOffset);
    void* tags = FieldOf(state, TagsOffset);
    if (!groupsWritten || !SizeArray(Types.PerspectiveArray, perspectives, look.Perspectives.size()) ||
        !SizeArray(Types.TagArray, tags, look.Tags.size()))
    {
        log("looks: a look state could not be filled");
        return {};
    }
    for (uint32_t index = 0; index < look.Perspectives.size(); ++index)
    {
        void* perspective = Types.PerspectiveArray->GetElement(perspectives, index);
        WriteValue(Types.PerspectiveName, perspective, look.Perspectives[index].Name);
        WriteValue(Types.PerspectiveFpp, perspective, look.Perspectives[index].Fpp);
        WriteValue(Types.PerspectiveTpp, perspective, look.Perspectives[index].Tpp);
    }
    for (uint32_t index = 0; index < look.Tags.size(); ++index)
    {
        std::memcpy(Types.TagArray->GetElement(tags, index), &look.Tags[index], sizeof(uint64_t));
    }
    *static_cast<uint8_t*>(FieldOf(state, BuiltFlagOffset)) = 1;
    return stateHandle;
}
}

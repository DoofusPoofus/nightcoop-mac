#include "plugin/BodyRecords.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/gamedataTweakDBRecord.hpp>
#include <RED4ext/TweakDB.hpp>

#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace nightcoop_mac::body_records
{
namespace
{
const char* BaseRecordName = "Character.Player_Puppet_Base";
std::map<std::string, uint64_t> MadeRecords;

bool EndsWith(const std::string& text, const char* ending)
{
    const std::string tail(ending);
    return text.size() > tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

std::string PropertyNameOf(std::string functionName)
{
    if (functionName.rfind("Get", 0) == 0 && EndsWith(functionName, "Count"))
    {
        return functionName.substr(3, functionName.size() - 8);
    }
    if (functionName.rfind("Get", 0) == 0 && EndsWith(functionName, "Item"))
    {
        return functionName.substr(3, functionName.size() - 7);
    }
    if (EndsWith(functionName, "Contains"))
    {
        return functionName.substr(0, functionName.size() - 8);
    }
    if (functionName.rfind("Get", 0) == 0 && functionName.size() > 3)
    {
        return functionName.substr(3);
    }
    return functionName;
}

std::set<std::string> FlatNamesOf(RED4ext::CClass* recordClass)
{
    std::set<std::string> names;
    for (auto* classType = recordClass; classType; classType = classType->parent)
    {
        for (uint32_t index = 0; index < classType->funcs.Size(); ++index)
        {
            const char* shortName = classType->funcs[index]->shortName.ToString();
            if (!shortName || !*shortName)
            {
                continue;
            }
            std::string property = PropertyNameOf(shortName);
            if (property.empty())
            {
                continue;
            }
            names.insert(property);
            property[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(property[0])));
            names.insert(property);
        }
    }
    return names;
}

bool CopyFlat(RED4ext::TweakDB* tweakDb, const std::string& fromFlat, const std::string& toFlat)
{
    auto* value = tweakDb->GetFlatValue(RED4ext::TweakDBID(fromFlat));
    if (!value)
    {
        return false;
    }
    RED4ext::TweakDBID copied(toFlat);
    copied.SetTDBOffset(value->ToTDBOffset());
    tweakDb->AddFlat(copied);
    return true;
}
}

uint64_t PlayerBodyRecord(const std::string& templatePath, bool male, const LogFunction& log)
{
    const auto made = MadeRecords.find(templatePath);
    if (made != MadeRecords.end())
    {
        return made->second;
    }
    auto* tweakDb = RED4ext::TweakDB::Get();
    if (!tweakDb)
    {
        log("records: no TweakDB");
        return 0;
    }
    const RED4ext::TweakDBID baseId(BaseRecordName);
    const auto baseRecord = tweakDb->GetRecord(baseId);
    if (!baseRecord)
    {
        log(std::string("records: base record ") + BaseRecordName + " not found");
        return 0;
    }
    const RED4ext::ResourcePath templateHash(templatePath.c_str());
    char suffix[24];
    std::snprintf(suffix, sizeof(suffix), "_g%010llx", static_cast<unsigned long long>(templateHash.hash & 0xFFFFFFFFFFull));
    const std::string recordName = std::string(BaseRecordName) + suffix;
    const RED4ext::TweakDBID recordId(recordName);
    if (tweakDb->GetRecord(recordId))
    {
        MadeRecords[templatePath] = recordId.value;
        return recordId.value;
    }

    auto* recordClass = baseRecord.instance->GetType();
    uint32_t copiedFlats = 0;
    for (const auto& flatName : FlatNamesOf(recordClass))
    {
        if (flatName == "entityTemplatePath" || flatName == "genders")
        {
            continue;
        }
        copiedFlats += CopyFlat(tweakDb, std::string(BaseRecordName) + "." + flatName, recordName + "." + flatName) ? 1 : 0;
    }

    auto* resourceType = RED4ext::CRTTISystem::Get()->GetType("raRef:CResource");
    if (!resourceType)
    {
        log("records: no raRef:CResource type");
        return 0;
    }
    RED4ext::ResourcePath templateValue(templatePath.c_str());
    const int32_t templateOffset = tweakDb->CreateFlatValue(RED4ext::CStackType(resourceType, &templateValue));
    if (templateOffset < 0)
    {
        log("records: no room for a new entityTemplatePath value in TweakDB");
        return 0;
    }
    RED4ext::TweakDBID templateFlat(recordName + ".entityTemplatePath");
    templateFlat.SetTDBOffset(templateOffset);
    tweakDb->AddFlat(templateFlat);

    const std::string gendersFrom = std::string(male ? "Character.TPP_Player_Cutscene_Male" : "Character.TPP_Player_Cutscene_Female") + ".genders";
    const bool gendersCopied = CopyFlat(tweakDb, gendersFrom, recordName + ".genders") ||
                               CopyFlat(tweakDb, std::string(BaseRecordName) + ".genders", recordName + ".genders");

    const auto* tweakRecord = reinterpret_cast<RED4ext::gamedataTweakDBRecord*>(baseRecord.instance);
    tweakDb->CreateRecord(recordId, tweakRecord->GetTweakBaseHash());
    const bool recordThere = static_cast<bool>(tweakDb->GetRecord(recordId));
    char line[400];
    std::snprintf(line, sizeof(line), "records: %s %s for %s (%u flat(s) copied from %s, genders %s)", recordName.c_str(),
                  recordThere ? "made" : "NOT made", templatePath.c_str(), copiedFlats, BaseRecordName,
                  gendersCopied ? (male ? "male" : "female") : "missing");
    log(line);
    if (!recordThere)
    {
        return 0;
    }
    MadeRecords[templatePath] = recordId.value;
    return recordId.value;
}
}

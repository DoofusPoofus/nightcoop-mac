#include "plugin/FactsDb.hpp"

#include "net/QuestMessages.hpp"
#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <mach-o/dyld.h>

#include <cstring>

namespace nightcoop_mac::facts_db
{
namespace
{
constexpr uintptr_t QuestsSystemVtable = 0x7a2f268;
constexpr size_t FactsDbGetterSlot = 0x278;
constexpr size_t FactsDbField = 0xf8;
constexpr uintptr_t FactsDbVtable = 0x79e2540;
constexpr size_t GetFactSlot = 0x18;
constexpr size_t SetFactSlot = 0x20;
constexpr size_t ListIdsSlot = 0x40;
constexpr size_t FirstTableField = 0x08;
constexpr size_t TableCountField = 0x58;
constexpr size_t TableIdCountField = 0x14;
constexpr uint32_t TablesInDb = 10;
constexpr uint32_t MostIdsInTable = 1000000;
constexpr uint32_t SpareIdRoom = 4096;

struct CodeSite
{
    const char* Name;
    uintptr_t Offset;
    std::vector<uint8_t> Bytes;
};

const CodeSite FactsDbGetter {"the quests systems facts db getter", 0x2a129d8, {0x00, 0x7c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}};
const CodeSite GetFactSite {"FactsDB GetFact", 0x27f23e4,
                            {0x28, 0x04, 0x00, 0x51, 0x08, 0x4c, 0x28, 0x8b, 0x00, 0x05, 0x40, 0xf9, 0xe1, 0x03, 0x02, 0xaa, 0xb0, 0xf9, 0xff, 0x17}};
const CodeSite SetFactSite {"FactsDB SetFact", 0x27f23f8, {0xff, 0x03, 0x02, 0xd1, 0xfd, 0x7b, 0x07, 0xa9, 0xfd, 0xc3, 0x01, 0x91,
                                                           0xe8, 0x03, 0x02, 0xaa, 0x29, 0x7b, 0x02, 0xd0, 0x29, 0xc1, 0x46, 0xf9}};
const CodeSite ListIdsSite {"FactsDB ListIds", 0x27f24f4,
                            {0x28, 0x04, 0x00, 0x51, 0x08, 0x4c, 0x28, 0x8b, 0x00, 0x05, 0x40, 0xf9, 0xe1, 0x03, 0x02, 0xaa, 0xc5, 0xfd, 0xff, 0x17}};
const CodeSite TableIdCopySite {"the facts tables id copy", 0x27f1c18, {0xf6, 0x57, 0xbd, 0xa9, 0xf4, 0x4f, 0x01, 0xa9, 0xfd, 0x7b, 0x02, 0xa9,
                                                                        0xfd, 0x83, 0x00, 0x91, 0xf4, 0x03, 0x01, 0xaa, 0xf3, 0x03, 0x00, 0xaa}};
const CodeSite TableRoomCheckSite {"the facts tables id copy room check", 0x27f1ccc,
                                   {0x61, 0x16, 0x40, 0xb9, 0x88, 0x0a, 0x40, 0xb9, 0x1f, 0x01, 0x01, 0x6b, 0xa3, 0x00, 0x00, 0x54, 0x68, 0x2a,
                                    0x40, 0xb9, 0x68, 0x01, 0x00, 0x37, 0x41, 0x05, 0x00, 0x35, 0x36, 0x00, 0x00, 0x14, 0xe0, 0x03, 0x14, 0xaa}};
const CodeSite TableBuildSite {"the facts db table setup", 0x27f2374, {0x9f, 0x5a, 0x00, 0xb9, 0x68, 0x5a, 0x40, 0xb9, 0x08, 0x05, 0x00, 0x11,
                                                                       0x68, 0x5a, 0x00, 0xb9, 0x74, 0x5a, 0x28, 0xf8, 0xb5, 0x06, 0x00, 0x71}};

const char* const ComparedFactNames[] = {"q000_done", "q001_mission0_complete", "q101_johnny_char_entry", "q101_done", "glossary_scavengers",
                                         "game_was_loaded"};

struct GameIdArray
{
    uint32_t* Entries = nullptr;
    uint32_t Capacity = 0;
    uint32_t Size = 0;
};

using GetFactFunction = int32_t (*)(void*, uint32_t, uint32_t);
using SetFactFunction = void (*)(void*, uint32_t, uint32_t, int32_t);
using ListIdsFunction = void (*)(void*, uint32_t, GameIdArray*);

enum class Lookup
{
    Found,
    NotYet,
    Wrong,
};

struct State
{
    bool CodeChecked = false;
    bool Disabled = false;
    uintptr_t Base = 0;
    RED4ext::CBaseFunction* GetQuestsSystem = nullptr;
    RED4ext::CBaseFunction* GetFactStr = nullptr;
    uint8_t* CurrentDb = nullptr;
    uint8_t* ComparedDb = nullptr;
};

State Facts;

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

bool CodeLooksRight(const CodeSite& site, const LogFunction& log)
{
    if (std::memcmp(reinterpret_cast<const void*>(Facts.Base + site.Offset), site.Bytes.data(), site.Bytes.size()) == 0)
    {
        return true;
    }
    log(std::string("quests: ") + site.Name + " is not the code seen in mac 2.5.0, the quest facts stay off");
    return false;
}

bool CheckCode(const LogFunction& log)
{
    Facts.Base = GameImageBase();
    if (!Facts.Base)
    {
        log("quests: no game image, the quest facts stay off");
        return false;
    }
    Facts.GetQuestsSystem = game::FindFunction("ScriptGameInstance", "GetQuestsSystem");
    Facts.GetFactStr = game::FindFunction("questQuestsSystem", "GetFactStr");
    if (!game::ParamsAre(Facts.GetQuestsSystem, {"ScriptGameInstance"}) || !game::ParamsAre(Facts.GetFactStr, {"String"}) ||
        !game::ReturnIs(Facts.GetFactStr, "Int32"))
    {
        log("quests: the quests system calls look different than expected (" + game::Describe(Facts.GetQuestsSystem) + ", " +
            game::Describe(Facts.GetFactStr) + "), the quest facts stay off");
        return false;
    }
    for (const CodeSite* site : {&FactsDbGetter, &GetFactSite, &SetFactSite, &ListIdsSite, &TableIdCopySite, &TableRoomCheckSite, &TableBuildSite})
    {
        if (!CodeLooksRight(*site, log))
        {
            return false;
        }
    }
    return true;
}

uintptr_t SlotOf(const void* object, size_t slot)
{
    const auto vtable = *reinterpret_cast<const uintptr_t*>(object);
    return *reinterpret_cast<const uintptr_t*>(vtable + slot);
}

Lookup FindLiveDb(RED4ext::Handle<RED4ext::IScriptable>& questsSystemHandle, std::string& problem)
{
    Facts.CurrentDb = nullptr;
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(Facts.GetQuestsSystem, &questsSystemHandle, args) || !questsSystemHandle)
    {
        return Lookup::NotYet;
    }
    auto* questsSystem = reinterpret_cast<uint8_t*>(questsSystemHandle.instance);
    if (*reinterpret_cast<const uintptr_t*>(questsSystem) != Facts.Base + QuestsSystemVtable)
    {
        problem = "the quests system does not have the vtable of mac 2.5.0";
        return Lookup::Wrong;
    }
    if (SlotOf(questsSystem, FactsDbGetterSlot) != Facts.Base + FactsDbGetter.Offset)
    {
        problem = "the quests systems facts db getter moved";
        return Lookup::Wrong;
    }
    auto* db = *reinterpret_cast<uint8_t**>(questsSystem + FactsDbField);
    if (!db)
    {
        return Lookup::NotYet;
    }
    if (*reinterpret_cast<const uintptr_t*>(db) != Facts.Base + FactsDbVtable || SlotOf(db, GetFactSlot) != Facts.Base + GetFactSite.Offset ||
        SlotOf(db, SetFactSlot) != Facts.Base + SetFactSite.Offset || SlotOf(db, ListIdsSlot) != Facts.Base + ListIdsSite.Offset)
    {
        problem = "the facts db does not have the vtable of mac 2.5.0";
        return Lookup::Wrong;
    }
    if (*reinterpret_cast<const uint32_t*>(db + TableCountField) != TablesInDb)
    {
        problem = "the facts db does not hold 10 tables";
        return Lookup::Wrong;
    }
    Facts.CurrentDb = db;
    return Lookup::Found;
}

int32_t ScriptFact(const RED4ext::Handle<RED4ext::IScriptable>& questsSystem, const char* factName)
{
    RED4ext::CString name(factName);
    int32_t value = 0;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &name);
    game::CallMethod(questsSystem.instance, Facts.GetFactStr, &value, args);
    return value;
}

bool MatchesScriptFacts(const RED4ext::Handle<RED4ext::IScriptable>& questsSystem, const LogFunction& log)
{
    std::string compared;
    for (const char* factName : ComparedFactNames)
    {
        int32_t ours = 0;
        ReadFact(net::kQuestFactsTable, net::FactIdOf(factName), ours);
        const int32_t scripts = ScriptFact(questsSystem, factName);
        if (ours != scripts)
        {
            log(std::string("quests: the facts db says ") + factName + " = " + std::to_string(ours) + " but the script says " + std::to_string(scripts) +
                ", the quest facts stay off");
            return false;
        }
        compared += std::string(compared.empty() ? "" : ", ") + factName + " = " + std::to_string(ours);
    }
    log("quests: the facts db reads like the scripts GetFactStr (" + compared + "), facts go by id now");
    return true;
}
}

bool Ready(const LogFunction& log)
{
    if (Facts.Disabled)
    {
        return false;
    }
    if (!Facts.CodeChecked)
    {
        Facts.CodeChecked = true;
        if (!CheckCode(log))
        {
            Facts.Disabled = true;
            return false;
        }
    }
    std::string problem;
    RED4ext::Handle<RED4ext::IScriptable> questsSystem;
    const Lookup lookup = FindLiveDb(questsSystem, problem);
    if (lookup == Lookup::Wrong)
    {
        log("quests: " + problem + ", the quest facts stay off");
        Facts.Disabled = true;
        return false;
    }
    if (lookup == Lookup::NotYet)
    {
        return false;
    }
    if (Facts.CurrentDb != Facts.ComparedDb)
    {
        if (!MatchesScriptFacts(questsSystem, log))
        {
            Facts.Disabled = true;
            Facts.CurrentDb = nullptr;
            return false;
        }
        Facts.ComparedDb = Facts.CurrentDb;
    }
    return true;
}

bool TurnedOff()
{
    return Facts.Disabled;
}

bool ReadFact(uint8_t table, uint32_t factId, int32_t& value)
{
    if (!Facts.CurrentDb || Facts.Disabled || table < 1 || table > TablesInDb)
    {
        return false;
    }
    const auto getFact = reinterpret_cast<GetFactFunction>(SlotOf(Facts.CurrentDb, GetFactSlot));
    value = getFact(Facts.CurrentDb, table, factId);
    return true;
}

bool WriteFact(uint8_t table, uint32_t factId, int32_t value)
{
    if (!Facts.CurrentDb || Facts.Disabled || table < 1 || table > TablesInDb)
    {
        return false;
    }
    const auto setFact = reinterpret_cast<SetFactFunction>(SlotOf(Facts.CurrentDb, SetFactSlot));
    setFact(Facts.CurrentDb, table, factId, value);
    return true;
}

bool ListTable(uint8_t table, std::vector<std::pair<uint32_t, int32_t>>& facts)
{
    facts.clear();
    if (!Facts.CurrentDb || Facts.Disabled || table < 1 || table > TablesInDb)
    {
        return false;
    }
    const auto* tableObject = *reinterpret_cast<uint8_t* const*>(Facts.CurrentDb + FirstTableField + (table - 1) * sizeof(void*));
    if (!tableObject)
    {
        return false;
    }
    const uint32_t idCount = *reinterpret_cast<const uint32_t*>(tableObject + TableIdCountField);
    if (idCount > MostIdsInTable)
    {
        return false;
    }
    std::vector<uint32_t> idRoom(static_cast<size_t>(idCount) * 2 + SpareIdRoom);
    GameIdArray ids;
    ids.Entries = idRoom.data();
    ids.Capacity = static_cast<uint32_t>(idRoom.size());
    const auto listIds = reinterpret_cast<ListIdsFunction>(SlotOf(Facts.CurrentDb, ListIdsSlot));
    listIds(Facts.CurrentDb, table, &ids);
    if (ids.Entries != idRoom.data() || ids.Size > idRoom.size())
    {
        Facts.Disabled = true;
        return false;
    }
    facts.reserve(ids.Size);
    for (uint32_t index = 0; index < ids.Size; ++index)
    {
        int32_t value = 0;
        ReadFact(table, idRoom[index], value);
        facts.emplace_back(idRoom[index], value);
    }
    return true;
}
}

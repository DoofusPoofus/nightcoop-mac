#include "plugin/WorldHandoff.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/SavePackage.hpp"
#include "net/WorldMessages.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"
#include "plugin/Items.hpp"
#include "plugin/PartnerRoster.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <crt_externs.h>
#include <spawn.h>
#include <sys/clonefile.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace nightcoop_mac::world_handoff
{
namespace
{
namespace fs = std::filesystem;

constexpr uint64_t SaveWaitMs = 60000;
constexpr uint64_t RestartFallbackMs = 15000;
constexpr uint64_t ApplyCharacterAfterMs = 15000;
constexpr uint64_t ReturnAfterMs = 30000;
constexpr uint64_t ReturnSaveWaitMs = 60000;
constexpr uint64_t QuitAfterRestartAskMs = 3000;
constexpr uint64_t SaveRetryMs = 2000;
constexpr uint32_t ChunksPerTick = 8;
constexpr uint64_t ListRetryMs = 31000;
constexpr uint32_t MostListTries = 3;
constexpr uint64_t NewestFallbackMs = 10000;
constexpr uint64_t CheckpointFallbackMs = 15000;
constexpr int32_t LoadStarted = 2;

enum class HostStage
{
    AskSave,
    WaitSave,
    Sending,
    Sent,
};

struct HostJob
{
    uint32_t Peer = 0;
    uint8_t Purpose = 1;
    uint32_t TransferId = 0;
    HostStage Stage = HostStage::AskSave;
    uint64_t StartedMs = 0;
    uint64_t NextTryMs = 0;
    fs::file_time_type AskedAt;
    std::string Expected;
    bool SlotZeroSaved = false;
    std::string Folder;
    uintmax_t LastSize = 0;
    std::vector<uint8_t> Metadata;
    std::vector<uint8_t> Save;
    size_t MetadataSent = 0;
    size_t SaveSent = 0;
    uint64_t MetadataHash = 0;
    uint64_t SaveHash = 0;
};

enum class GuestPhase
{
    Idle,
    Waiting,
    Loading,
    InHostWorld,
    Returning,
    SavingCharacter,
    Failed,
};

struct Incoming
{
    uint32_t TransferId = 0;
    std::string HostFolder;
    std::vector<uint8_t> Files[3];
    uint64_t Hashes[3] = {0, 0, 0};
    size_t Received[3] = {0, 0, 0};
};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* HasHandler = nullptr;
    RED4ext::CBaseFunction* MakeSave = nullptr;
    RED4ext::CBaseFunction* LoadFolder = nullptr;
    RED4ext::CBaseFunction* LoadState = nullptr;
};

Calls Game;
std::vector<HostJob> Jobs;
uint32_t TransferCounter = 0;
GuestPhase Phase = GuestPhase::Idle;
Incoming Arriving;
std::string WrittenFolder;
uint32_t ListTries = 0;
uint64_t NextListMs = 0;
bool LeftWorldWhileLoading = false;
uint64_t LoadAskedMs = 0;
bool NewestTried = false;
uint64_t NewestAskedMs = 0;
bool CheckpointTried = false;
bool IndexTried = false;
uint64_t IndexAskedMs = 0;
uint64_t CheckpointAskedMs = 0;
uint64_t QuitAtMs = 0;
bool ArrivalChecked = false;
fs::path PluginFolder;
bool RestartAllowed = true;
bool CharacterSplice = true;
uint64_t ApplyCharacterAtMs = 0;
uint64_t ArrivalNowMs = 0;
uint64_t TestCaptureAtMs = 0;
uint64_t TestApplyAtMs = 0;
std::string TestCaptured;
uint64_t NotInSessionSinceMs = 0;
HostJob ReturnJob;
uint64_t ReturnStartedMs = 0;
uint64_t ReturnNextTryMs = 0;
bool HostWasInSave = false;
uint32_t HostWorlds = 0;
HostJob CharacterJob;
uint64_t CharacterStartedMs = 0;
uint64_t CharacterNextTryMs = 0;
bool CharacterSaveAsked = false;
uint32_t SentAheadTransfer = 0;
bool HostWorldUpFromSentAhead = false;
bool AskAfterCharacterSave = false;
bool WeAreInHostWorld = false;
bool AutosavesChecked = false;
bool AutosaveTestWanted = false;
bool AutosaveTestDone = false;
std::map<std::string, int64_t> AutosaveTimes;
uint64_t NextAutosaveLookMs = 0;

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    Game.HasHandler = game::FindFunction("NightCoopWorldHandoff", "HasHandler");
    Game.MakeSave = game::FindFunction("NightCoopWorldHandoff", "MakeSave");
    Game.LoadFolder = game::FindFunction("NightCoopWorldHandoff", "LoadFolder");
    Game.LoadState = game::FindFunction("NightCoopWorldHandoff", "LoadState");
    Game.Usable = game::ParamsAre(Game.HasHandler, {}) && game::ParamsAre(Game.MakeSave, {"String"}) && game::ParamsAre(Game.LoadFolder, {"String"}) &&
                  game::ParamsAre(Game.LoadState, {});
    if (!Game.Usable)
    {
        log("handoff: no NightCoopWorldHandoff script service, the world handoff is off");
    }
    return Game.Usable;
}

bool CallBool(RED4ext::CBaseFunction* function)
{
    auto service = game::ScriptService("NightCoopWorldHandoff");
    bool answer = false;
    RED4ext::StackArgs_t noArgs;
    return service && game::CallMethod(service.instance, function, &answer, noArgs) && answer;
}

int32_t LoadState()
{
    auto service = game::ScriptService("NightCoopWorldHandoff");
    int32_t state = 0;
    RED4ext::StackArgs_t noArgs;
    if (service)
    {
        game::CallMethod(service.instance, Game.LoadState, &state, noArgs);
    }
    return state;
}


bool MakeSaveIn(const std::string& folder)
{
    auto service = game::ScriptService("NightCoopWorldHandoff");
    RED4ext::CString name(folder.c_str());
    bool asked = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &name);
    return service && game::CallMethod(service.instance, Game.MakeSave, &asked, args) && asked;
}

bool LoadFolder(const std::string& folder)
{
    auto service = game::ScriptService("NightCoopWorldHandoff");
    RED4ext::CString name(folder.c_str());
    bool asked = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &name);
    return service && game::CallMethod(service.instance, Game.LoadFolder, &asked, args) && asked;
}

fs::path SavesFolder()
{
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / "Library/Application Support/CD Projekt Red/Cyberpunk 2077/saves";
}

std::vector<uint8_t> ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void SendStatus(net::RelaySession& session, uint8_t code, uint32_t value, const std::string& text, int32_t toPeer)
{
    session.SendGameMessage(net::WriteWorldStatus({code, value, text}), toPeer);
}

bool GuardSlotZero(HostJob& job, const LogFunction& log)
{
    std::error_code error;
    const fs::path slotZero = SavesFolder() / "ManualSave-0";
    const fs::path copy = PluginFolder / "handoff" / "slot0-copy";
    fs::remove_all(copy, error);
    job.SlotZeroSaved = false;
    if (!fs::exists(slotZero, error))
    {
        return true;
    }
    fs::create_directories(copy.parent_path(), error);
    fs::copy(slotZero, copy, fs::copy_options::recursive, error);
    if (error)
    {
        log("handoff: could not copy ManualSave-0 aside (" + error.message() + "), no save made so nothing gets written over");
        return false;
    }
    for (const auto& file : fs::directory_iterator(slotZero, error))
    {
        std::error_code timeError;
        fs::last_write_time(copy / file.path().filename(), fs::last_write_time(file.path(), timeError), timeError);
    }
    job.SlotZeroSaved = true;
    return true;
}

uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string NextManualSave()
{
    int highest = -1;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(SavesFolder(), error))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("ManualSave-", 0) == 0)
        {
            highest = std::max(highest, std::atoi(name.c_str() + 11));
        }
    }
    return "ManualSave-" + std::to_string(highest + 1);
}

bool FindNewSave(HostJob& job, std::error_code& error)
{
    fs::path newest;
    fs::file_time_type newestTime;
    for (const auto& entry : fs::directory_iterator(SavesFolder(), error))
    {
        const std::string entryName = entry.path().filename().string();
        if (!job.Expected.empty() && entryName != job.Expected && entryName != "ManualSave-0")
        {
            continue;
        }
        const auto save = entry.path() / "sav.dat";
        const auto written = fs::last_write_time(save, error);
        if (error || written < job.AskedAt - std::chrono::seconds(2))
        {
            error.clear();
            continue;
        }
        if (newest.empty() || written > newestTime)
        {
            newest = entry.path();
            newestTime = written;
        }
    }
    if (newest.empty())
    {
        return false;
    }
    const uintmax_t size = fs::file_size(newest / "sav.dat", error);
    if (error || size == 0 || size != job.LastSize || job.Folder != newest.filename().string())
    {
        job.LastSize = error ? 0 : size;
        job.Folder = newest.filename().string();
        error.clear();
        return false;
    }
    if (newest.filename().string() == "ManualSave-0" && !job.Expected.empty() && job.Expected != "ManualSave-0")
    {
        const fs::path target = SavesFolder() / job.Expected;
        fs::rename(newest, target, error);
        if (!error && job.SlotZeroSaved)
        {
            fs::rename(PluginFolder / "handoff" / "slot0-copy", newest, error);
        }
        if (error)
        {
            return false;
        }
        newest = target;
        job.Folder = job.Expected;
        for (const auto& file : fs::directory_iterator(newest, error))
        {
            const std::string name = file.path().filename().string();
            if (name.rfind("metadata.", 0) == 0)
            {
                std::ifstream input(file.path());
                std::string metadata((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                input.close();
                const std::string nameKey = "\"name\": \"";
                const size_t nameAt = metadata.find(nameKey);
                const size_t nameEnd = nameAt == std::string::npos ? std::string::npos : metadata.find('"', nameAt + nameKey.size());
                if (nameEnd != std::string::npos)
                {
                    metadata.replace(nameAt + nameKey.size(), nameEnd - nameAt - nameKey.size(), job.Expected);
                    std::ofstream(file.path(), std::ios::binary).write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
                }
            }
        }
    }
    for (const auto& file : fs::directory_iterator(newest, error))
    {
        const std::string name = file.path().filename().string();
        if (name.rfind("metadata.", 0) == 0 && file.path().extension() == ".json")
        {
            job.Metadata = ReadFile(file.path());
        }
    }
    job.Save = ReadFile(newest / "sav.dat");
    job.MetadataHash = net::WorldFileHash(job.Metadata);
    job.SaveHash = net::WorldFileHash(job.Save);
    return !job.Save.empty() && !job.Metadata.empty();
}

void SendChunks(HostJob& job, net::RelaySession& session)
{
    uint32_t sentThisTick = 0;
    while (sentThisTick < ChunksPerTick && (job.MetadataSent < job.Metadata.size() || job.SaveSent < job.Save.size()))
    {
        const bool metadataFirst = job.MetadataSent < job.Metadata.size();
        const auto& bytes = metadataFirst ? job.Metadata : job.Save;
        size_t& sent = metadataFirst ? job.MetadataSent : job.SaveSent;
        net::WorldFile chunk;
        chunk.TransferId = job.TransferId;
        chunk.Kind = metadataFirst ? net::kWorldFileMetadata : net::kWorldFileSave;
        chunk.Purpose = job.Purpose;
        chunk.TotalSize = static_cast<uint32_t>(bytes.size());
        chunk.Hash = metadataFirst ? job.MetadataHash : job.SaveHash;
        chunk.Offset = static_cast<uint32_t>(sent);
        const size_t length = std::min<size_t>(net::kWorldChunkBytes, bytes.size() - sent);
        chunk.Bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(sent), bytes.begin() + static_cast<std::ptrdiff_t>(sent + length));
        session.SendGameMessage(net::WriteWorldFile(chunk), static_cast<int32_t>(job.Peer));
        sent += length;
        ++sentThisTick;
    }
}

void ServeJobs(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    for (auto job = Jobs.begin(); job != Jobs.end();)
    {
        if (!job->StartedMs)
        {
            job->StartedMs = nowMs;
        }
        if (job->Stage == HostStage::AskSave && nowMs >= job->NextTryMs)
        {
            job->NextTryMs = nowMs + SaveRetryMs;
            job->Expected = NextManualSave();
            if (inSave && CallBool(Game.HasHandler) && GuardSlotZero(*job, log) && MakeSaveIn(job->Expected))
            {
                job->Stage = HostStage::WaitSave;
                job->AskedAt = fs::file_time_type::clock::now();
                job->StartedMs = nowMs;
                log("handoff: making a save of our world for peer " + std::to_string(job->Peer));
            }
            else if (nowMs - job->StartedMs > SaveWaitMs)
            {
                SendStatus(session, 4, 0, "the save did not answer", static_cast<int32_t>(job->Peer));
                job = Jobs.erase(job);
                continue;
            }
        }
        else if (job->Stage == HostStage::WaitSave && nowMs >= job->NextTryMs)
        {
            job->NextTryMs = nowMs + 500;
            std::error_code error;
            if (FindNewSave(*job, error))
            {
                job->Stage = HostStage::Sending;
                SendStatus(session, 3, job->TransferId, job->Folder, static_cast<int32_t>(job->Peer));
                log("handoff: our world (" + job->Folder + ", " + std::to_string(job->Save.size()) + " bytes) goes to peer " + std::to_string(job->Peer));
            }
            else if (nowMs - job->StartedMs > SaveWaitMs)
            {
                SendStatus(session, 4, 0, "the new save is not on the disk", static_cast<int32_t>(job->Peer));
                log("handoff: the save for peer " + std::to_string(job->Peer) + " never showed up on the disk");
                job = Jobs.erase(job);
                continue;
            }
        }
        else if (job->Stage == HostStage::Sending)
        {
            SendChunks(*job, session);
            if (job->MetadataSent >= job->Metadata.size() && job->SaveSent >= job->Save.size())
            {
                job->Stage = HostStage::Sent;
                job->Metadata.clear();
                job->Save.clear();
                log("handoff: our world is sent to peer " + std::to_string(job->Peer));
            }
        }
        ++job;
    }
}


std::map<std::string, int64_t> ReadAutosaveTimes()
{
    std::map<std::string, int64_t> times;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(SavesFolder(), error))
    {
        const std::string name = entry.path().filename().string();
        std::error_code timeError;
        const auto written = fs::last_write_time(entry.path() / "sav.dat", timeError);
        if (name.rfind("AutoSave-", 0) == 0 && !timeError)
        {
            times[name] = static_cast<int64_t>(written.time_since_epoch().count());
        }
    }
    return times;
}

bool CloneFolder(const fs::path& from, const fs::path& to)
{
    if (clonefile(from.c_str(), to.c_str(), 0) == 0)
    {
        return true;
    }
    std::error_code error;
    fs::copy(from, to, fs::copy_options::recursive, error);
    return !error;
}

void BackUpAutosaves(const LogFunction& log)
{
    std::error_code error;
    const fs::path backup = PluginFolder / "handoff" / "autosaves";
    if (!fs::exists(backup, error))
    {
        fs::create_directories(backup, error);
        int cloned = 0;
        for (const auto& [name, written] : ReadAutosaveTimes())
        {
            cloned += CloneFolder(SavesFolder() / name, backup / name) ? 1 : 0;
        }
        log("handoff: our " + std::to_string(cloned) + " autosaves are kept aside while we play in the hosts world");
    }
    AutosaveTimes = ReadAutosaveTimes();
}

void WatchAutosaves(uint64_t nowMs, const LogFunction& log)
{
    if (nowMs < NextAutosaveLookMs)
    {
        return;
    }
    NextAutosaveLookMs = nowMs + 5000;
    for (const auto& [name, written] : ReadAutosaveTimes())
    {
        const auto known = AutosaveTimes.find(name);
        if (known != AutosaveTimes.end() && known->second == written)
        {
            continue;
        }
        AutosaveTimes[name] = written;
        std::ofstream(PluginFolder / "handoff" / "host-autosaves.txt", std::ios::app) << "autosave " << name << " " << written << "\n";
        log("handoff: the game autosaved the hosts world into " + name + ", our own goes back there once we are home");
    }
}

void RestoreAutosaves(const LogFunction& log)
{
    std::error_code error;
    const fs::path backup = PluginFolder / "handoff" / "autosaves";
    const fs::path list = PluginFolder / "handoff" / "host-autosaves.txt";
    if (!fs::exists(backup, error))
    {
        return;
    }
    std::map<std::string, int64_t> hostAutosaves;
    {
        std::ifstream input(list);
        std::string word;
        std::string name;
        int64_t written = 0;
        while (input >> word >> name >> written)
        {
            hostAutosaves[name] = written;
        }
    }
    const auto timesNow = ReadAutosaveTimes();
    const fs::path aside = PluginFolder / "handoff" / "host-autosaves";
    fs::create_directories(aside, error);
    int restored = 0;
    int leftAlone = 0;
    for (const auto& [name, written] : hostAutosaves)
    {
        const auto now = timesNow.find(name);
        if (now == timesNow.end() || now->second != written)
        {
            ++leftAlone;
            continue;
        }
        std::error_code moveError;
        fs::remove_all(aside / name, moveError);
        fs::rename(SavesFolder() / name, aside / name, moveError);
        if (moveError)
        {
            ++leftAlone;
            continue;
        }
        if (fs::exists(backup / name, moveError))
        {
            fs::rename(backup / name, SavesFolder() / name, moveError);
            if (moveError)
            {
                CloneFolder(backup / name, SavesFolder() / name);
            }
        }
        ++restored;
    }
    fs::remove_all(backup, error);
    fs::remove(list, error);
    if (!hostAutosaves.empty())
    {
        log("handoff: " + std::to_string(restored) + " autosaves the game wrote in the hosts world went to handoff/host-autosaves and ours are back (" +
            std::to_string(leftAlone) + " changed since and stay)");
    }
}

fs::path OwnCharacterFolder()
{
    std::error_code error;
    std::string listed;
    {
        std::ifstream list(PluginFolder / "handoff" / "mod-saves.txt");
        listed.assign((std::istreambuf_iterator<char>(list)), std::istreambuf_iterator<char>());
        std::ifstream hostAutosaves(PluginFolder / "handoff" / "host-autosaves.txt");
        listed.append((std::istreambuf_iterator<char>(hostAutosaves)), std::istreambuf_iterator<char>());
    }
    fs::path newest;
    fs::file_time_type newestTime {};
    for (const auto& entry : fs::directory_iterator(SavesFolder(), error))
    {
        const std::string name = entry.path().filename().string();
        const fs::path save = entry.path() / "sav.dat";
        if (!entry.is_directory(error) || !fs::exists(save, error) || name.rfind("PointOfNoReturn", 0) == 0 || name.rfind("EndGameSave", 0) == 0 ||
            listed.find(" " + name + " ") != std::string::npos)
        {
            continue;
        }
        const auto written = fs::last_write_time(save, error);
        if (newest.empty() || written > newestTime)
        {
            newest = entry.path();
            newestTime = written;
        }
    }
    return newest;
}

std::string SaveBlockReason(const std::string& folder)
{
    static auto* blockReason = game::FindFunction("NightCoopWorldHandoff", "SaveBlockReason");
    auto service = game::ScriptService("NightCoopWorldHandoff");
    if (!blockReason || !service)
    {
        return "the handoff script is not there";
    }
    RED4ext::CString result;
    RED4ext::CString name(folder.c_str());
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &name);
    game::CallMethod(service.instance, blockReason, &result, args);
    return result.c_str();
}

std::string CallCharacter(const char* functionName, const std::string& text)
{
    auto service = game::ScriptService("NightCoopCharacter");
    auto* function = game::FindFunction("NightCoopCharacter", functionName);
    if (!service || !function)
    {
        return {};
    }
    RED4ext::CString result;
    RED4ext::CString argument(text.c_str());
    RED4ext::StackArgs_t args;
    if (!text.empty() || std::string(functionName) == "Apply")
    {
        args.emplace_back(nullptr, &argument);
    }
    game::CallMethod(service.instance, function, &result, args);
    return result.c_str();
}

std::string EquippedLines()
{
    auto service = game::ScriptService("NightCoopCharacter");
    auto* slotCount = game::FindFunction("NightCoopCharacter", "SlotCount");
    auto* getEquipped = game::FindFunction("NightCoopCharacter", "GetEquipped");
    auto* areas = RED4ext::CRTTISystem::Get()->GetEnum("gamedataEquipmentArea");
    if (!service || !slotCount || !getEquipped || !areas)
    {
        return {};
    }
    std::string lines;
    for (uint32_t index = 0; index < areas->valueList.Size(); ++index)
    {
        int32_t area = static_cast<int32_t>(areas->valueList[index]);
        int32_t slots = 0;
        RED4ext::StackArgs_t countArgs;
        countArgs.emplace_back(nullptr, &area);
        game::CallMethod(service.instance, slotCount, &slots, countArgs);
        for (int32_t slot = 0; slot < slots && slot < 16; ++slot)
        {
            items::ItemId item;
            RED4ext::StackArgs_t slotArgs;
            slotArgs.emplace_back(nullptr, &area);
            slotArgs.emplace_back(nullptr, &slot);
            if (!game::CallMethod(service.instance, getEquipped, &item, slotArgs) || !item.Record)
            {
                continue;
            }
            const auto* raw = reinterpret_cast<const uint8_t*>(&item);
            char hex[33];
            for (int byte = 0; byte < 16; ++byte)
            {
                std::snprintf(hex + byte * 2, 3, "%02x", raw[byte]);
            }
            lines += "equip " + std::to_string(area) + " " + std::to_string(slot) + " " + hex + "\n";
            static auto* itemStats = game::FindFunction("NightCoopCharacter", "ItemStats");
            RED4ext::CString stats;
            RED4ext::StackArgs_t statArgs;
            statArgs.emplace_back(nullptr, &item);
            if (itemStats && game::CallMethod(service.instance, itemStats, &stats, statArgs) && stats.Length() > 0)
            {
                lines += "istat " + std::string(hex) + " " + stats.c_str() + "\n";
            }
        }
    }
    return lines;
}

int32_t InventoryCount()
{
    auto service = game::ScriptService("NightCoopCharacter");
    auto* count = game::FindFunction("NightCoopCharacter", "InventoryCount");
    int32_t items = -1;
    RED4ext::StackArgs_t noArgs;
    if (service && count)
    {
        game::CallMethod(service.instance, count, &items, noArgs);
    }
    return items;
}

int FixItemStats(const std::string& character, int& itemsFixed)
{
    auto service = game::ScriptService("NightCoopCharacter");
    static auto* applyItemStats = game::FindFunction("NightCoopCharacter", "ApplyItemStats");
    itemsFixed = 0;
    if (!service || !applyItemStats)
    {
        return 0;
    }
    int statsFixed = 0;
    std::istringstream lines(character);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.rfind("istat ", 0) != 0 || line.size() < 6 + 32)
        {
            continue;
        }
        const std::string hex = line.substr(6, 32);
        items::ItemId item;
        auto* raw = reinterpret_cast<uint8_t*>(&item);
        for (int byte = 0; byte < 16; ++byte)
        {
            raw[byte] = static_cast<uint8_t>(std::stoul(hex.substr(byte * 2, 2), nullptr, 16));
        }
        RED4ext::CString wanted(line.size() > 39 ? line.substr(39).c_str() : "");
        int32_t fixed = 0;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &item);
        args.emplace_back(nullptr, &wanted);
        game::CallMethod(service.instance, applyItemStats, &fixed, args);
        if (fixed > 0)
        {
            ++itemsFixed;
            statsFixed += fixed;
        }
    }
    return statsFixed;
}

int EquipFromLines(const std::string& character, int& missing)
{
    auto service = game::ScriptService("NightCoopCharacter");
    auto* equipNow = game::FindFunction("NightCoopCharacter", "EquipNow");
    auto* hasInInventory = game::FindFunction("NightCoopCharacter", "HasInInventory");
    missing = 0;
    if (!service || !equipNow || !hasInInventory)
    {
        return 0;
    }
    int equipped = 0;
    std::istringstream lines(character);
    std::string word;
    while (lines >> word)
    {
        if (word != "equip")
        {
            continue;
        }
        int32_t area = 0;
        int32_t slot = 0;
        std::string hex;
        if (!(lines >> area >> slot >> hex) || hex.size() != 32)
        {
            continue;
        }
        items::ItemId item;
        auto* raw = reinterpret_cast<uint8_t*>(&item);
        for (int byte = 0; byte < 16; ++byte)
        {
            raw[byte] = static_cast<uint8_t>(std::stoul(hex.substr(byte * 2, 2), nullptr, 16));
        }
        bool there = false;
        RED4ext::StackArgs_t hasArgs;
        hasArgs.emplace_back(nullptr, &item);
        game::CallMethod(service.instance, hasInInventory, &there, hasArgs);
        if (!there)
        {
            ++missing;
            continue;
        }
        bool done = false;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &area);
        args.emplace_back(nullptr, &slot);
        args.emplace_back(nullptr, &item);
        game::CallMethod(service.instance, equipNow, &done, args);
        equipped += done ? 1 : 0;
    }
    return equipped;
}

void CaptureCharacter(const LogFunction& log)
{
    const std::string character = CallCharacter("Capture", "") + EquippedLines();
    if (character.empty())
    {
        log("handoff: could not read our level and perks");
        return;
    }
    std::error_code error;
    fs::create_directories(PluginFolder / "handoff", error);
    std::ofstream(PluginFolder / "handoff" / "character.txt") << character;
    log("handoff: our level attributes perks and gear are kept for the next world (" + std::to_string(std::count(character.begin(), character.end(), '\n')) +
        " values, " + std::to_string(InventoryCount()) + " items in our inventory)");
}

void ApplyCharacter(const LogFunction& log)
{
    const fs::path file = PluginFolder / "handoff" / "character.txt";
    std::ifstream input(file);
    const std::string character((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::error_code error;
    fs::remove(file, error);
    if (character.empty())
    {
        return;
    }
    log("handoff: our level attributes and perks here: " + CallCharacter("Apply", character));
    int missing = 0;
    const int equipped = EquipFromLines(character, missing);
    log("handoff: " + std::to_string(equipped) + " of our equipped items are back on, " + std::to_string(missing) + " not in our inventory (it has " +
        std::to_string(InventoryCount()) + " items)");
    int itemsFixed = 0;
    const int statsFixed = FixItemStats(character, itemsFixed);
    log("handoff: item stats (quality plus upgrades level iconic): " + std::to_string(statsFixed) + " value(s) on " + std::to_string(itemsFixed) +
        " item(s) were off here and got put back");
}

std::vector<uint8_t> WithOurCharacter(const std::vector<uint8_t>& hostSave, const LogFunction& log)
{
    if (!CharacterSplice)
    {
        return hostSave;
    }
    CaptureCharacter(log);
    std::error_code reloadError;
    const fs::path ownFolder = !CharacterJob.Folder.empty() && fs::exists(SavesFolder() / CharacterJob.Folder / "sav.dat", reloadError)
                                   ? SavesFolder() / CharacterJob.Folder
                                   : OwnCharacterFolder();
    if (ownFolder.empty())
    {
        log("handoff: no save of our own found, we play the hosts V");
        return hostSave;
    }
    std::error_code stateError;
    if (!fs::exists(PluginFolder / "handoff" / "state.txt", stateError))
    {
        std::ofstream(PluginFolder / "handoff" / "state.txt") << "campaign " << ownFolder.filename().string() << "\n";
    }
    if (!CharacterJob.Folder.empty())
    {
        std::ofstream(PluginFolder / "handoff" / "mod-saves.txt", std::ios::app) << "reload " << CharacterJob.Folder << " 0 from us\n";
        CharacterJob = HostJob {};
    }
    save_package::SaveFile host;
    save_package::SaveFile guest;
    std::string error;
    if (!save_package::Read(hostSave, host, error))
    {
        log("handoff: the hosts save does not read (" + error + "), we play the hosts V");
        return hostSave;
    }
    if (!save_package::Read(ReadFile(ownFolder / "sav.dat"), guest, error))
    {
        log("handoff: our save " + ownFolder.filename().string() + " does not read (" + error + "), we play the hosts V");
        return hostSave;
    }
    std::vector<std::string> problems;
    const int swapped = save_package::SpliceCharacter(host, guest, problems);
    for (const auto& problem : problems)
    {
        log("handoff: " + problem);
    }
    const bool itemsTaken = save_package::SpliceInventories(host, guest, {save_package::kPlayerInventory, save_package::kStashInventory}, error);
    if (!itemsTaken)
    {
        log("handoff: our items stay out of the hosts world (" + error + ")");
    }
    log("handoff: our V from " + ownFolder.filename().string() + " goes into the hosts world (" + std::to_string(swapped) + " of " +
        std::to_string(save_package::CharacterNodeNames().size()) + " character nodes" + (itemsTaken ? ", our items and stash" : "") +
        "; level attributes and perks follow once we are there)");
    return save_package::Write(host);
}

bool WriteHostWorld(const LogFunction& log)
{
    const std::string folder = NextManualSave();
    const fs::path target = SavesFolder() / folder;
    std::error_code error;
    if (fs::exists(target, error) || !fs::create_directory(target, error))
    {
        log("handoff: could not make the save folder " + folder);
        return false;
    }
    std::string metadata(Arriving.Files[net::kWorldFileMetadata].begin(), Arriving.Files[net::kWorldFileMetadata].end());
    const std::string nameKey = "\"name\": \"";
    const size_t nameAt = metadata.find(nameKey);
    const size_t nameEnd = nameAt == std::string::npos ? std::string::npos : metadata.find('"', nameAt + nameKey.size());
    if (nameEnd != std::string::npos)
    {
        metadata.replace(nameAt + nameKey.size(), nameEnd - nameAt - nameKey.size(), folder);
    }
    const std::string stampKey = "\"timestampString\": \"";
    const size_t stampAt = metadata.find(stampKey);
    const size_t stampEnd = stampAt == std::string::npos ? std::string::npos : metadata.find('"', stampAt + stampKey.size());
    if (stampEnd != std::string::npos)
    {
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S, %d.%m.%Y", std::localtime(&now));
        metadata.replace(stampAt + stampKey.size(), stampEnd - stampAt - stampKey.size(), stamp);
    }
    std::ofstream(target / "metadata.9.json", std::ios::binary).write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    const auto built = WithOurCharacter(Arriving.Files[net::kWorldFileSave], log);
    std::ofstream(target / "sav.dat", std::ios::binary).write(reinterpret_cast<const char*>(built.data()), static_cast<std::streamsize>(built.size()));
    fs::create_directories(PluginFolder / "handoff", error);
    std::ofstream(PluginFolder / "handoff" / "mod-saves.txt", std::ios::app) << "join " << folder << " " << built.size() << " from " << Arriving.HostFolder << "\n";
    WrittenFolder = folder;
    log("handoff: the hosts world is written as " + folder + " (" + std::to_string(built.size()) + " bytes)");
    return true;
}

void FinishArrival(net::RelaySession& session, const LogFunction& log);

void TakeChunk(const net::WorldFile& chunk, net::RelaySession& session, const LogFunction& log)
{
    if (chunk.TransferId != Arriving.TransferId)
    {
        Arriving = Incoming {};
        Arriving.TransferId = chunk.TransferId;
    }
    auto& file = Arriving.Files[chunk.Kind];
    if (file.size() != chunk.TotalSize)
    {
        file.assign(chunk.TotalSize, 0);
        Arriving.Received[chunk.Kind] = 0;
    }
    std::copy(chunk.Bytes.begin(), chunk.Bytes.end(), file.begin() + chunk.Offset);
    Arriving.Received[chunk.Kind] += chunk.Bytes.size();
    Arriving.Hashes[chunk.Kind] = chunk.Hash;
    const bool metadataDone = Arriving.Received[net::kWorldFileMetadata] >= Arriving.Files[net::kWorldFileMetadata].size() &&
                              !Arriving.Files[net::kWorldFileMetadata].empty();
    const bool saveDone = Arriving.Received[net::kWorldFileSave] >= Arriving.Files[net::kWorldFileSave].size() && !Arriving.Files[net::kWorldFileSave].empty();
    if (!metadataDone || !saveDone)
    {
        return;
    }
    FinishArrival(session, log);
}

void FinishArrival(net::RelaySession& session, const LogFunction& log)
{
    const bool metadataDone = Arriving.Received[net::kWorldFileMetadata] >= Arriving.Files[net::kWorldFileMetadata].size() &&
                              !Arriving.Files[net::kWorldFileMetadata].empty();
    const bool saveDone = Arriving.Received[net::kWorldFileSave] >= Arriving.Files[net::kWorldFileSave].size() && !Arriving.Files[net::kWorldFileSave].empty();
    if (!metadataDone || !saveDone || Phase != GuestPhase::Waiting)
    {
        return;
    }
    if (SentAheadTransfer && SentAheadTransfer == Arriving.TransferId && !HostWorldUpFromSentAhead)
    {
        return;
    }
    if (net::WorldFileHash(Arriving.Files[net::kWorldFileSave]) != Arriving.Hashes[net::kWorldFileSave] ||
        net::WorldFileHash(Arriving.Files[net::kWorldFileMetadata]) != Arriving.Hashes[net::kWorldFileMetadata])
    {
        SendStatus(session, 8, 0, "the host's save came broken (hash)", 0);
        log("handoff: the hosts save came broken (hash)");
        Phase = GuestPhase::Failed;
        return;
    }
    if (!WriteHostWorld(log))
    {
        SendStatus(session, 8, 0, "could not write the host's save", 0);
        Phase = GuestPhase::Failed;
        return;
    }
    Phase = GuestPhase::Loading;
    ListTries = 0;
    NextListMs = 0;
    LeftWorldWhileLoading = false;
    LoadAskedMs = 0;
    NewestTried = false;
    NewestAskedMs = 0;
    CheckpointTried = false;
    IndexTried = false;
    IndexAskedMs = 0;
    CheckpointAskedMs = 0;
}
}

std::string SaveFolderFromArguments()
{
    char** arguments = *_NSGetArgv();
    const int count = *_NSGetArgc();
    for (int index = 0; index < count; ++index)
    {
        const std::string argument = arguments[index] ? arguments[index] : "";
        if (argument.rfind("-save=", 0) == 0)
        {
            return argument.substr(6);
        }
    }
    return {};
}

bool RestartInto(const std::string& folder, uint32_t transferId, const std::string& kind, const LogFunction& log)
{
    const fs::path gameFolder = PluginFolder.parent_path().parent_path().parent_path();
    std::error_code error;
    if (!fs::exists(gameFolder / "launch_red4ext.sh", error))
    {
        log("handoff: no launch_red4ext.sh in " + gameFolder.string() + ", cant restart into the hosts world");
        return false;
    }
    std::ofstream(PluginFolder / "handoff" / "arriving.txt") << folder << " " << transferId << " " << kind << "\n";
    const std::string command = "sleep 3; while /usr/bin/pgrep -x Cyberpunk2077 >/dev/null; do sleep 1; done; cd '" + gameFolder.string() +
                                "' && ./launch_red4ext.sh -save=" + folder + " -skipStartScreen >/dev/null 2>&1";
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
    const char* shellArguments[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    pid_t relauncher = 0;
    const int result = posix_spawn(&relauncher, "/bin/sh", nullptr, &attributes, const_cast<char* const*>(shellArguments), *_NSGetEnviron());
    posix_spawnattr_destroy(&attributes);
    if (result != 0)
    {
        log("handoff: could not start the relauncher (" + std::to_string(result) + ")");
        return false;
    }
    log("handoff: the game restarts straight into " + folder + " (relauncher " + std::to_string(relauncher) + ")");
    return true;
}

void CheckArrival(bool inSave, net::RelaySession& session, const LogFunction& log)
{
    std::error_code error;
    const fs::path marker = PluginFolder / "handoff" / "arriving.txt";
    if (!fs::exists(marker, error))
    {
        ArrivalChecked = true;
        return;
    }
    if (!inSave)
    {
        return;
    }
    ArrivalChecked = true;
    std::string folder;
    uint32_t transferId = 0;
    std::string kind;
    std::ifstream(marker) >> folder >> transferId >> kind;
    fs::remove(marker, error);
    if (folder.empty() || folder != SaveFolderFromArguments())
    {
        log("handoff: an old restart marker for " + folder + " was left, this start is not it");
        return;
    }
    if (kind == "home")
    {
        Phase = GuestPhase::Idle;
        ApplyCharacterAtMs = ArrivalNowMs + ApplyCharacterAfterMs;
        fs::remove(PluginFolder / "handoff" / "state.txt", error);
        log("handoff: we are back in our own world (" + folder + ") with what we got in the hosts");
        hud::Show("Back in your own world", 5.0f);
        return;
    }
    WrittenFolder = folder;
    Phase = GuestPhase::InHostWorld;
    WeAreInHostWorld = true;
    BackUpAutosaves(log);
    ApplyCharacterAtMs = ArrivalNowMs + ApplyCharacterAfterMs;
    SendStatus(session, 7, transferId, "", 0);
    log("handoff: we are in the hosts world now (" + folder + ", after a restart)");
    hud::Show("You are in the host's world", 5.0f);
}

fs::path CampaignPath(const std::string& campaign)
{
    std::error_code error;
    const fs::path kept = PluginFolder / "handoff" / "autosaves" / campaign;
    return fs::exists(kept / "sav.dat", error) ? kept : SavesFolder() / campaign;
}

std::string CampaignFolder()
{
    std::string word;
    std::string folder;
    std::ifstream(PluginFolder / "handoff" / "state.txt") >> word >> folder;
    return word == "campaign" ? folder : std::string();
}

void StartReturn(uint64_t nowMs, const LogFunction& log)
{
    if (CampaignFolder().empty())
    {
        log("handoff: the session is gone but we dont know our own campaign save, we stay");
        NotInSessionSinceMs = 0;
        return;
    }
    const std::string sessionFolder = NextManualSave();
    HostJob guard;
    if (!CallBool(Game.HasHandler) || !GuardSlotZero(guard, log) || !MakeSaveIn(sessionFolder))
    {
        log("handoff: the session is gone, cant save here yet (" + SaveBlockReason(sessionFolder) + "), trying again");
        NotInSessionSinceMs = nowMs - ReturnAfterMs + 5000;
        return;
    }
    ReturnJob = HostJob {};
    ReturnJob.Expected = sessionFolder;
    ReturnJob.SlotZeroSaved = guard.SlotZeroSaved;
    ReturnJob.AskedAt = fs::file_time_type::clock::now();
    ReturnStartedMs = nowMs;
    ReturnNextTryMs = nowMs + SaveRetryMs;
    Phase = GuestPhase::Returning;
    log("handoff: the session is gone, saving what we got in the hosts world to take it home");
    hud::Show("Going back to your own world", 4.0f);
}

void TickReturn(uint64_t nowMs, const LogFunction& log)
{
    if (QuitAtMs || nowMs < ReturnNextTryMs)
    {
        return;
    }
    ReturnNextTryMs = nowMs + SaveRetryMs;
    std::error_code error;
    const std::string campaign = CampaignFolder();
    std::vector<uint8_t> built;
    if (FindNewSave(ReturnJob, error))
    {
        std::ofstream(PluginFolder / "handoff" / "mod-saves.txt", std::ios::app) << "session " << ReturnJob.Folder << " " << ReturnJob.Save.size() << " from us\n";
        save_package::SaveFile home;
        save_package::SaveFile session;
        std::string readError;
        if (save_package::Read(ReadFile(CampaignPath(campaign) / "sav.dat"), home, readError) && save_package::Read(ReturnJob.Save, session, readError))
        {
            std::vector<std::string> problems;
            const int swapped = save_package::SpliceCharacter(home, session, problems);
            const bool items = save_package::SpliceInventories(home, session, {save_package::kPlayerInventory, save_package::kStashInventory}, readError);
            built = save_package::Write(home);
            log("handoff: our V from the session (" + std::to_string(swapped) + " character nodes" + (items ? ", items and stash" : "") + ") goes back into " + campaign);
        }
        else
        {
            log("handoff: " + readError + ", we go back to " + campaign + " as it was");
        }
    }
    else if (nowMs - ReturnStartedMs < ReturnSaveWaitMs)
    {
        return;
    }
    else
    {
        log("handoff: our save in the hosts world never showed up, we go back to " + campaign + " as it was");
    }
    std::string target = campaign;
    if (!built.empty())
    {
        target = NextManualSave();
        const fs::path folder = SavesFolder() / target;
        fs::create_directory(folder, error);
        for (const auto& file : fs::directory_iterator(CampaignPath(campaign), error))
        {
            const std::string name = file.path().filename().string();
            if (name != "sav.dat")
            {
                fs::copy_file(file.path(), folder / name, fs::copy_options::overwrite_existing, error);
            }
        }
        for (const auto& file : fs::directory_iterator(folder, error))
        {
            const std::string name = file.path().filename().string();
            if (name.rfind("metadata.", 0) == 0)
            {
                std::ifstream input(file.path());
                std::string metadata((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                const std::string nameKey = "\"name\": \"";
                const size_t nameAt = metadata.find(nameKey);
                const size_t nameEnd = nameAt == std::string::npos ? std::string::npos : metadata.find('"', nameAt + nameKey.size());
                if (nameEnd != std::string::npos)
                {
                    metadata.replace(nameAt + nameKey.size(), nameEnd - nameAt - nameKey.size(), target);
                }
                const std::string stampKey = "\"timestampString\": \"";
                const size_t stampAt = metadata.find(stampKey);
                const size_t stampEnd = stampAt == std::string::npos ? std::string::npos : metadata.find('"', stampAt + stampKey.size());
                if (stampEnd != std::string::npos)
                {
                    const std::time_t now = std::time(nullptr);
                    char stamp[32];
                    std::strftime(stamp, sizeof(stamp), "%H:%M:%S, %d.%m.%Y", std::localtime(&now));
                    metadata.replace(stampAt + stampKey.size(), stampEnd - stampAt - stampKey.size(), stamp);
                }
                std::ofstream(file.path(), std::ios::binary).write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
            }
        }
        std::ofstream(folder / "sav.dat", std::ios::binary).write(reinterpret_cast<const char*>(built.data()), static_cast<std::streamsize>(built.size()));
    }
    CaptureCharacter(log);
    if (RestartInto(target, 0, "home", log))
    {
        hud::Show("Restarting into your own world", 3.0f);
        QuitAtMs = nowMs + QuitAfterRestartAskMs;
    }
    else
    {
        Phase = GuestPhase::InHostWorld;
        NotInSessionSinceMs = 0;
    }
}

void Tick(uint64_t nowMs, bool inSave, bool joinHostWorld, net::RelaySession& session, const fs::path& pluginFolder, const LogFunction& log)
{
    PluginFolder = pluginFolder;
    const auto status = session.GetStatus();
    if (inSave && WeAreInHostWorld)
    {
        WatchAutosaves(nowMs, log);
        if (AutosaveTestWanted && !AutosaveTestDone && ArrivalNowMs && nowMs >= ArrivalNowMs + 40000)
        {
            AutosaveTestDone = true;
            static auto* testAutosave = game::FindFunction("NightCoopWorldHandoff", "TestAutosave");
            auto service = game::ScriptService("NightCoopWorldHandoff");
            bool asked = false;
            RED4ext::StackArgs_t noArgs;
            if (testAutosave && service)
            {
                game::CallMethod(service.instance, testAutosave, &asked, noArgs);
            }
            log(std::string("handoff: test_autosave asked the game for an autosave in the hosts world (") + (asked ? "it said yes" : "it said no") + ")");
        }
    }
    else if (inSave && !AutosavesChecked)
    {
        std::error_code error;
        if (!fs::exists(PluginFolder / "handoff" / "arriving.txt", error))
        {
            AutosavesChecked = true;
            RestoreAutosaves(log);
        }
    }
    if (!Resolve(log))
    {
        return;
    }
    if (QuitAtMs && nowMs >= QuitAtMs)
    {
        log("handoff: closing the game for the restart");
        _exit(0);
    }
    if (Phase == GuestPhase::InHostWorld && RestartAllowed && inSave)
    {
        if (status.InSession)
        {
            NotInSessionSinceMs = 0;
        }
        else if (!NotInSessionSinceMs)
        {
            NotInSessionSinceMs = nowMs;
        }
        else if (nowMs - NotInSessionSinceMs >= ReturnAfterMs)
        {
            StartReturn(nowMs, log);
        }
    }
    if (Phase == GuestPhase::Returning)
    {
        TickReturn(nowMs, log);
        return;
    }
    if (Phase == GuestPhase::SavingCharacter && nowMs >= CharacterNextTryMs)
    {
        CharacterNextTryMs = nowMs + SaveRetryMs;
        if (!CharacterSaveAsked)
        {
            CharacterJob.Expected = NextManualSave();
            CharacterJob.AskedAt = fs::file_time_type::clock::now();
            CharacterSaveAsked = inSave && CallBool(Game.HasHandler) && GuardSlotZero(CharacterJob, log) && MakeSaveIn(CharacterJob.Expected);
            if (CharacterSaveAsked)
            {
                log("handoff: saving our V in " + CharacterJob.Expected + " before the hosts new world comes");
            }
            else if (nowMs - CharacterStartedMs > ReturnSaveWaitMs)
            {
                log("handoff: the game would not save our V here (" + SaveBlockReason(CharacterJob.Expected) + "), our last V goes into the hosts new world");
                Phase = GuestPhase::Waiting;
                if (AskAfterCharacterSave)
                {
                    AskAfterCharacterSave = false;
                    session.SendGameMessage(net::WriteWorldWant({3, 0}), 0);
                }
                FinishArrival(session, log);
            }
            return;
        }
        std::error_code error;
        const bool saved = FindNewSave(CharacterJob, error);
        if (saved || nowMs - CharacterStartedMs > ReturnSaveWaitMs)
        {
            log(saved ? "handoff: our V is saved in " + CharacterJob.Folder + ", waiting for the hosts new world"
                      : "handoff: our save for the hosts new world never showed up, our last V goes in");
            Phase = GuestPhase::Waiting;
            if (AskAfterCharacterSave)
            {
                AskAfterCharacterSave = false;
                session.SendGameMessage(net::WriteWorldWant({3, 0}), 0);
                log("handoff: asked the host for its current world (WorldWant 3)");
            }
            FinishArrival(session, log);
        }
        return;
    }
    if (status.InSession && status.MyPeerNumber == 0)
    {
        if (inSave && !HostWasInSave)
        {
            ++HostWorlds;
            if (HostWorlds >= 2)
            {
                for (const auto& [peer, nickname] : session.PeerNicknames())
                {
                    if (peer == 0 || !partner_roster::PeerInHostWorld(peer))
                    {
                        continue;
                    }
                    session.SendGameMessage(net::WriteWorldReload({1, 0, "world " + std::to_string(HostWorlds)}), static_cast<int32_t>(peer));
                    Jobs.erase(std::remove_if(Jobs.begin(), Jobs.end(), [peer](const HostJob& job) { return job.Peer == peer; }), Jobs.end());
                    HostJob job;
                    job.Peer = peer;
                    job.Purpose = 4;
                    if (++TransferCounter == 0)
                    {
                        ++TransferCounter;
                    }
                    job.TransferId = TransferCounter;
                    Jobs.push_back(job);
                    log("handoff: we loaded another save, peer " + std::to_string(peer) + " in our old world gets the new one");
                }
            }
        }
        HostWasInSave = inSave;
    }
    if (joinHostWorld && !(status.InSession && status.MyPeerNumber == 0))
    {
        if (ApplyCharacterAtMs && inSave && nowMs >= ApplyCharacterAtMs)
        {
            ApplyCharacterAtMs = 0;
            ApplyCharacter(log);
        }
        if (!ArrivalChecked)
        {
            ArrivalNowMs = nowMs;
            CheckArrival(inSave, session, log);
            if (!ArrivalChecked || Phase == GuestPhase::InHostWorld)
            {
                return;
            }
        }
    }
    if (!status.InSession)
    {
        return;
    }
    if (status.MyPeerNumber == 0)
    {
        ServeJobs(nowMs, inSave, session, log);
        return;
    }
    if (!joinHostWorld)
    {
        return;
    }
    if (QuitAtMs && nowMs >= QuitAtMs)
    {
        log("handoff: closing the game for the restart");
        _exit(0);
    }
    if (Phase == GuestPhase::Idle && inSave && partner_roster::HostInItsWorld())
    {
        session.SendGameMessage(net::WriteWorldWant({1, 0}), 0);
        Phase = GuestPhase::Waiting;
        log("handoff: the host is in its world, asking for it");
        hud::Show("Waiting for the host's world", 4.0f);
    }
    else if (Phase == GuestPhase::Loading)
    {
        if (!inSave)
        {
            LeftWorldWhileLoading = true;
        }
        else if (LeftWorldWhileLoading)
        {
            Phase = GuestPhase::InHostWorld;
            ApplyCharacterAtMs = nowMs + ApplyCharacterAfterMs;
            SendStatus(session, 7, Arriving.TransferId, "", 0);
            log("handoff: we are in the hosts world now (" + WrittenFolder + ")");
            hud::Show("You are in the host's world", 5.0f);
            return;
        }
        std::error_code flagError;
        const fs::path loadsNeverWork = PluginFolder / "handoff" / "loads-never-work.txt";
        if (RestartAllowed && !QuitAtMs && !CheckpointAskedMs && fs::exists(loadsNeverWork, flagError))
        {
            log("handoff: in game loads never worked here before, restarting right away");
            CheckpointAskedMs = nowMs - RestartFallbackMs;
        }
        static auto* loadNewest = game::FindFunction("NightCoopWorldHandoff", "LoadNewest");
        if (!NewestTried && LoadAskedMs && LoadState() == 1 && nowMs - LoadAskedMs >= NewestFallbackMs && loadNewest)
        {
            NewestTried = true;
            auto service = game::ScriptService("NightCoopWorldHandoff");
            bool asked = false;
            RED4ext::StackArgs_t noArgs;
            static auto* hasMenu = game::FindFunction("NightCoopWorldHandoff", "HasMenu");
            bool menuThere = false;
            if (service && hasMenu)
            {
                game::CallMethod(service.instance, hasMenu, &menuThere, noArgs);
            }
            if (service && game::CallMethod(service.instance, loadNewest, &asked, noArgs) && asked)
            {
                NewestAskedMs = nowMs;
                log("handoff: the save list did not answer, loading the newest save (" + WrittenFolder + ") instead" +
                    (menuThere ? " with the in game menus quick load" : " with LoadLastCheckpoint (no in game menu)"));
            }
        }
        static auto* loadIndex = game::FindFunction("NightCoopWorldHandoff", "LoadIndex");
        if (!IndexTried && NewestAskedMs && nowMs - NewestAskedMs >= CheckpointFallbackMs && loadIndex)
        {
            IndexTried = true;
            IndexAskedMs = nowMs;
            auto service = game::ScriptService("NightCoopWorldHandoff");
            int32_t newestIndex = 0;
            bool asked = false;
            RED4ext::StackArgs_t indexArgs;
            indexArgs.emplace_back(nullptr, &newestIndex);
            if (service && game::CallMethod(service.instance, loadIndex, &asked, indexArgs) && asked)
            {
                log("handoff: the quick load did nothing, loading the first save of the list (ours is the newest)");
            }
        }
        static auto* loadCheckpoint = game::FindFunction("NightCoopWorldHandoff", "LoadCheckpoint");
        if (!CheckpointTried && IndexAskedMs && nowMs - IndexAskedMs >= CheckpointFallbackMs && loadCheckpoint)
        {
            CheckpointTried = true;
            auto service = game::ScriptService("NightCoopWorldHandoff");
            bool asked = false;
            RED4ext::StackArgs_t noArgs;
            if (service && game::CallMethod(service.instance, loadCheckpoint, &asked, noArgs) && asked)
            {
                log("handoff: loading by index did nothing, trying LoadLastCheckpoint(false)");
            }
            CheckpointAskedMs = nowMs;
        }
        if (RestartAllowed && CheckpointAskedMs && !QuitAtMs && nowMs - CheckpointAskedMs >= RestartFallbackMs)
        {
            if (CharacterSplice)
            {
                CaptureCharacter(log);
            }
            if (RestartInto(WrittenFolder, Arriving.TransferId, "host", log))
            {
                std::ofstream(loadsNeverWork) << "the save list callback quick load LoadSavedGame and LoadLastCheckpoint all stayed quiet, the handoff restarts the game\n";
                hud::Show("Restarting into the host's world", 3.0f);
                QuitAtMs = nowMs + QuitAfterRestartAskMs;
            }
            else
            {
                CheckpointAskedMs = 0;
            }
        }
        if (nowMs >= NextListMs && LoadState() != LoadStarted)
        {
            static auto* listed = game::FindFunction("NightCoopWorldHandoff", "ListedSaves");
            static auto* sample = game::FindFunction("NightCoopWorldHandoff", "ListSample");
            auto service = game::ScriptService("NightCoopWorldHandoff");
            int32_t listedCount = -1;
            RED4ext::CString listedNames;
            RED4ext::StackArgs_t noArgs;
            if (service && ListTries > 0)
            {
                game::CallMethod(service.instance, listed, &listedCount, noArgs);
                game::CallMethod(service.instance, sample, &listedNames, noArgs);
                log("handoff: load state " + std::to_string(LoadState()) + ", the save list had " + std::to_string(listedCount) + " save(s): " +
                    listedNames.c_str());
            }
            if (ListTries >= MostListTries)
            {
                SendStatus(session, 8, 0, "the host's save did not show up in our save list", 0);
                log("handoff: " + WrittenFolder + " never showed up in the save list");
                Phase = GuestPhase::Failed;
                return;
            }
            ++ListTries;
            NextListMs = nowMs + ListRetryMs;
            if (LoadFolder(WrittenFolder))
            {
                LoadAskedMs = LoadAskedMs ? LoadAskedMs : nowMs;
                SendStatus(session, 6, Arriving.TransferId, "", 0);
                log("handoff: loading " + WrittenFolder + " (try " + std::to_string(ListTries) + ")");
            }
            else
            {
                log("handoff: no save handler from the game yet, " + WrittenFolder + " waits");
            }
        }
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    if (!Game.Usable)
    {
        return;
    }
    const uint8_t type = net::MessageTypeOf(message);
    const bool weAreHost = session.GetStatus().MyPeerNumber == 0;
    net::WorldWant want;
    net::WorldFile chunk;
    net::WorldStatus worldStatus;
    if (weAreHost && type == static_cast<uint8_t>(nc::proto::MsgType::WorldWant) && net::ReadWorldWant(message, want))
    {
        Jobs.erase(std::remove_if(Jobs.begin(), Jobs.end(), [fromPeer](const HostJob& job) { return job.Peer == fromPeer; }), Jobs.end());
        HostJob job;
        job.Peer = fromPeer;
        job.Purpose = want.Reason;
        if (++TransferCounter == 0)
        {
            ++TransferCounter;
        }
        job.TransferId = TransferCounter;
        Jobs.push_back(job);
        log("handoff: peer " + std::to_string(fromPeer) + " wants our world (reason " + std::to_string(want.Reason) + ")");
    }
    else if (weAreHost && type == static_cast<uint8_t>(nc::proto::MsgType::WorldStatus) && net::ReadWorldStatus(message, worldStatus))
    {
        const std::string name = partner_roster::NameOf(fromPeer, session);
        if (worldStatus.Code == 6)
        {
            hud::Show(name + " is loading your world", 4.0f);
        }
        else if (worldStatus.Code == 7)
        {
            hud::Show(name + " is in your world", 4.0f);
            Jobs.erase(std::remove_if(Jobs.begin(), Jobs.end(), [fromPeer](const HostJob& job) { return job.Peer == fromPeer; }), Jobs.end());
        }
        else if (worldStatus.Code == 8)
        {
            Jobs.erase(std::remove_if(Jobs.begin(), Jobs.end(), [fromPeer](const HostJob& job) { return job.Peer == fromPeer; }), Jobs.end());
        }
        log("handoff: peer " + std::to_string(fromPeer) + " says " + std::to_string(worldStatus.Code) + (worldStatus.Text.empty() ? "" : " (" + worldStatus.Text + ")"));
    }
    else if (!weAreHost && fromPeer == 0 && type == static_cast<uint8_t>(nc::proto::MsgType::WorldReload) && net::ReadWorldReload(message, worldStatus))
    {
        log("handoff: the host says its world reloads (kind " + std::to_string(worldStatus.Code) + ")");
        if ((worldStatus.Code == 1 || worldStatus.Code == 2) && Phase == GuestPhase::InHostWorld)
        {
            Arriving = Incoming {};
            CharacterJob = HostJob {};
            CharacterSaveAsked = false;
            SentAheadTransfer = worldStatus.Code == 2 ? worldStatus.Value : 0;
            HostWorldUpFromSentAhead = false;
            Phase = GuestPhase::SavingCharacter;
            CharacterStartedMs = SteadyNowMs();
            CharacterNextTryMs = CharacterStartedMs;
            hud::Show("The host loaded another save, you follow", 4.0f);
        }
        else if (worldStatus.Code == 3 && SentAheadTransfer && worldStatus.Value == SentAheadTransfer)
        {
            HostWorldUpFromSentAhead = true;
            log("handoff: the hosts world is up from the save it sent ahead");
            FinishArrival(session, log);
        }
        else if (worldStatus.Code == 4 && SentAheadTransfer)
        {
            SentAheadTransfer = 0;
            Arriving = Incoming {};
            log("handoff: the hosts world came up from another save, a fresh one comes");
        }
        else if (worldStatus.Code == 5 && SentAheadTransfer)
        {
            SentAheadTransfer = 0;
            Arriving = Incoming {};
            if (Phase == GuestPhase::SavingCharacter || Phase == GuestPhase::Waiting)
            {
                Phase = GuestPhase::InHostWorld;
            }
            log("handoff: the host did not load after all, we stay");
        }
    }
    else if (!weAreHost && fromPeer == 0 && type == static_cast<uint8_t>(nc::proto::MsgType::WorldFile) && net::ReadWorldFile(message, chunk))
    {
        TakeChunk(chunk, session, log);
    }
    else if (!weAreHost && fromPeer == 0 && type == static_cast<uint8_t>(nc::proto::MsgType::WorldStatus) && net::ReadWorldStatus(message, worldStatus))
    {
        if (worldStatus.Code == 3)
        {
            if (Arriving.TransferId != worldStatus.Value)
            {
                Arriving = Incoming {};
                Arriving.TransferId = worldStatus.Value;
            }
            Arriving.HostFolder = worldStatus.Text;
        }
        else if (worldStatus.Code == 4)
        {
            hud::Show("The host: " + worldStatus.Text, 5.0f);
            Phase = GuestPhase::Failed;
        }
        else if (worldStatus.Code == 1)
        {
            hud::Show("The host cannot save now (" + worldStatus.Text + "): the join goes as soon as it can", 5.0f);
        }
        else if (worldStatus.Code == 9 && Phase == GuestPhase::InHostWorld)
        {
            Arriving = Incoming {};
            CharacterJob = HostJob {};
            CharacterSaveAsked = false;
            SentAheadTransfer = 0;
            AskAfterCharacterSave = true;
            Phase = GuestPhase::SavingCharacter;
            CharacterStartedMs = SteadyNowMs();
            CharacterNextTryMs = CharacterStartedMs;
            hud::Show("We are in an older world of the host, its current one comes", 4.0f);
        }
        log("handoff: the host says " + std::to_string(worldStatus.Code) + (worldStatus.Text.empty() ? "" : " (" + worldStatus.Text + ")"));
    }
}

void AllowRestart(bool allowed)
{
    RestartAllowed = allowed;
}

void AskForAutosave(bool wanted)
{
    AutosaveTestWanted = wanted;
}

void KeepOurCharacter(bool keep)
{
    CharacterSplice = keep;
}

void TestCharacter(uint64_t nowMs, bool inSave, const LogFunction& log)
{
    if (!inSave)
    {
        return;
    }
    if (!TestCaptureAtMs)
    {
        TestCaptureAtMs = nowMs + 20000;
    }
    if (TestCaptureAtMs != 1 && nowMs >= TestCaptureAtMs)
    {
        TestCaptureAtMs = 1;
        TestCaptured = CallCharacter("Capture", "") + EquippedLines();
        TestApplyAtMs = nowMs + 10000;
        log("test character: captured " + std::to_string(std::count(TestCaptured.begin(), TestCaptured.end(), '\n')) + " values");
    }
    if (TestApplyAtMs && nowMs >= TestApplyAtMs)
    {
        TestApplyAtMs = 0;
        log("test character: applied the same values back: " + CallCharacter("Apply", TestCaptured));
        int missing = 0;
        log("test character: " + std::to_string(EquipFromLines(TestCaptured, missing)) + " equipped item(s) put on again, " + std::to_string(missing) +
            " missing");
        const std::string after = CallCharacter("Capture", "") + EquippedLines();
        log(std::string("test character: a new capture is ") + (after == TestCaptured ? "the same" : "different"));
        if (after != TestCaptured)
        {
            log("test character: before\n" + TestCaptured + "after\n" + after);
        }
    }
}

bool InHostWorld()
{
    return Phase == GuestPhase::InHostWorld;
}
}

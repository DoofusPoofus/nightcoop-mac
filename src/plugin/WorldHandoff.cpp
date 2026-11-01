#include "plugin/WorldHandoff.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/WorldMessages.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"
#include "plugin/PartnerRoster.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <crt_externs.h>
#include <spawn.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace nightcoop_mac::world_handoff
{
namespace
{
namespace fs = std::filesystem;

constexpr uint64_t SaveWaitMs = 60000;
constexpr uint64_t RestartFallbackMs = 15000;
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
    Game.Usable = game::ParamsAre(Game.HasHandler, {}) && game::ParamsAre(Game.MakeSave, {}) && game::ParamsAre(Game.LoadFolder, {"String"}) &&
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

bool FindNewSave(HostJob& job, std::error_code& error)
{
    fs::path newest;
    fs::file_time_type newestTime;
    for (const auto& entry : fs::directory_iterator(SavesFolder(), error))
    {
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
            if (inSave && CallBool(Game.HasHandler) && CallBool(Game.MakeSave))
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
    std::ofstream(target / "sav.dat", std::ios::binary)
        .write(reinterpret_cast<const char*>(Arriving.Files[net::kWorldFileSave].data()), static_cast<std::streamsize>(Arriving.Files[net::kWorldFileSave].size()));
    fs::create_directories(PluginFolder / "handoff", error);
    std::ofstream(PluginFolder / "handoff" / "mod-saves.txt", std::ios::app)
        << "join " << folder << " " << Arriving.Files[net::kWorldFileSave].size() << " from " << Arriving.HostFolder << "\n";
    WrittenFolder = folder;
    log("handoff: the hosts world is written as " + folder + " (" + std::to_string(Arriving.Files[net::kWorldFileSave].size()) + " bytes)");
    return true;
}

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
    if (!metadataDone || !saveDone || Phase != GuestPhase::Waiting)
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

bool RestartInto(const std::string& folder, uint32_t transferId, const LogFunction& log)
{
    const fs::path gameFolder = PluginFolder.parent_path().parent_path().parent_path();
    std::error_code error;
    if (!fs::exists(gameFolder / "launch_red4ext.sh", error))
    {
        log("handoff: no launch_red4ext.sh in " + gameFolder.string() + ", cant restart into the hosts world");
        return false;
    }
    std::ofstream(PluginFolder / "handoff" / "arriving.txt") << folder << " " << transferId << "\n";
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
    std::ifstream(marker) >> folder >> transferId;
    fs::remove(marker, error);
    if (folder.empty() || folder != SaveFolderFromArguments())
    {
        log("handoff: an old restart marker for " + folder + " was left, this start is not it");
        return;
    }
    WrittenFolder = folder;
    Phase = GuestPhase::InHostWorld;
    SendStatus(session, 7, transferId, "", 0);
    log("handoff: we are in the hosts world now (" + folder + ", after a restart)");
    hud::Show("You are in the host's world", 5.0f);
}

void Tick(uint64_t nowMs, bool inSave, bool joinHostWorld, net::RelaySession& session, const fs::path& pluginFolder, const LogFunction& log)
{
    PluginFolder = pluginFolder;
    const auto status = session.GetStatus();
    if (!status.InSession || !Resolve(log))
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
    if (!ArrivalChecked)
    {
        CheckArrival(inSave, session, log);
        if (!ArrivalChecked || Phase == GuestPhase::InHostWorld)
        {
            return;
        }
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
            SendStatus(session, 7, Arriving.TransferId, "", 0);
            log("handoff: we are in the hosts world now (" + WrittenFolder + ")");
            hud::Show("You are in the host's world", 5.0f);
            return;
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
            if (RestartInto(WrittenFolder, Arriving.TransferId, log))
            {
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
        log("handoff: the host says " + std::to_string(worldStatus.Code) + (worldStatus.Text.empty() ? "" : " (" + worldStatus.Text + ")"));
    }
}

void AllowRestart(bool allowed)
{
    RestartAllowed = allowed;
}

bool InHostWorld()
{
    return Phase == GuestPhase::InHostWorld;
}
}

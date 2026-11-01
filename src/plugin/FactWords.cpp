#include "plugin/FactWords.hpp"

#include "net/RelaySession.hpp"
#include "plugin/FactsDb.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/QuestSync.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace nightcoop_mac::fact_words
{
namespace
{
constexpr uint8_t DepositType = 175;
constexpr uint8_t DocumentType = 187;
constexpr uint8_t DeviceType = 246;
constexpr uint8_t AckType = 52;
constexpr uint64_t BaselineEveryMs = 500;
constexpr uint64_t WindowMs = 1000;
constexpr uint64_t ResendEveryMs = 3000;
constexpr int MostTries = 5;
constexpr size_t FactsPerWord = 16;

struct Word
{
    uint8_t Kind = 0;
    uint32_t Number = 0;
    std::vector<std::pair<uint32_t, int32_t>> Facts;
    uint64_t NextSendMs = 0;
    int Tries = 0;
};

std::vector<std::pair<uint32_t, int32_t>> Baseline;
uint64_t NextBaselineMs = 0;
std::map<uint8_t, uint64_t> WindowEndsMs;
std::vector<Word> Unacked;
uint32_t NextNumber = 1;
std::set<std::tuple<uint32_t, uint8_t, uint32_t>> TakenWords;

uint8_t TypeOfKind(uint8_t kind)
{
    return kind == 1 ? DepositType : kind == 2 ? DocumentType : DeviceType;
}

uint8_t KindOfType(uint8_t type)
{
    return type == DepositType ? 1 : type == DocumentType ? 2 : type == DeviceType ? 3 : 0;
}

const char* KindName(uint8_t kind)
{
    return kind == 1 ? "quest item deposit" : kind == 2 ? "document read" : "device action";
}

std::vector<uint8_t> WriteWord(const Word& word)
{
    nc::proto::Writer writer(static_cast<nc::proto::MsgType>(TypeOfKind(word.Kind)));
    writer.PutU8(static_cast<uint8_t>(word.Facts.size()));
    for (const auto& [factId, value] : word.Facts)
    {
        writer.PutU32(factId);
        writer.PutU32(static_cast<uint32_t>(value));
    }
    writer.PutU32(word.Number);
    return writer.Data();
}
}

bool Handles(uint8_t messageType)
{
    return KindOfType(messageType) != 0 || messageType == AckType;
}

void Tick(uint64_t nowMs, bool inSave, bool guestInHostWorld, bool testWord, net::RelaySession& session, const LogFunction& log)
{
    static auto* takeKinds = game::FindFunction("NightCoopFactWords", "TakeKinds");
    static uint64_t testAtMs = 0;
    auto service = game::ScriptService("NightCoopFactWords");
    if (testWord && inSave && guestInHostWorld && service && !Baseline.empty())
    {
        testAtMs = testAtMs ? testAtMs : nowMs + 15000;
        static auto* runTest = game::FindFunction("NightCoopFactWords", "TestWord");
        if (testAtMs != 1 && nowMs >= testAtMs && runTest)
        {
            testAtMs = 1;
            RED4ext::StackArgs_t noArgs;
            game::CallMethod(service.instance, runTest, nullptr, noArgs);
            log("fact words: test_factword read a made up document");
        }
    }
    RED4ext::DynArray<int32_t> kinds;
    RED4ext::StackArgs_t noArgs;
    if (takeKinds && service)
    {
        game::CallMethod(service.instance, takeKinds, &kinds, noArgs);
    }
    if (!inSave || !guestInHostWorld || !session.GetStatus().InSession || !facts_db::Ready(log))
    {
        Baseline.clear();
        WindowEndsMs.clear();
        return;
    }
    for (const int32_t kind : kinds)
    {
        if (kind >= 1 && kind <= 3 && !Baseline.empty() && !WindowEndsMs.count(static_cast<uint8_t>(kind)))
        {
            WindowEndsMs[static_cast<uint8_t>(kind)] = nowMs + WindowMs;
        }
    }
    for (auto window = WindowEndsMs.begin(); window != WindowEndsMs.end();)
    {
        if (nowMs < window->second)
        {
            ++window;
            continue;
        }
        std::vector<std::pair<uint32_t, int32_t>> now;
        if (facts_db::ListTable(1, now))
        {
            const auto changes = quest_sync::SharedChanges(Baseline, now);
            for (size_t first = 0; first < changes.size(); first += FactsPerWord)
            {
                Word word;
                word.Kind = window->first;
                word.Number = NextNumber++;
                word.Facts.assign(changes.begin() + static_cast<std::ptrdiff_t>(first),
                                  changes.begin() + static_cast<std::ptrdiff_t>(std::min(changes.size(), first + FactsPerWord)));
                Unacked.push_back(word);
            }
            log("fact words: our " + std::string(KindName(window->first)) + " changed " + std::to_string(changes.size()) + " fact(s), the host gets them");
            Baseline = now;
            NextBaselineMs = nowMs + BaselineEveryMs;
        }
        window = WindowEndsMs.erase(window);
    }
    if (WindowEndsMs.empty() && nowMs >= NextBaselineMs)
    {
        NextBaselineMs = nowMs + BaselineEveryMs;
        facts_db::ListTable(1, Baseline);
    }
    for (auto word = Unacked.begin(); word != Unacked.end();)
    {
        if (word->Tries >= MostTries)
        {
            log("fact words: the host never acked our word #" + std::to_string(word->Number) + ", dropped");
            word = Unacked.erase(word);
            continue;
        }
        if (nowMs >= word->NextSendMs)
        {
            ++word->Tries;
            word->NextSendMs = nowMs + ResendEveryMs;
            session.SendGameMessage(WriteWord(*word), 0);
        }
        ++word;
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool peerInOurWorld, net::RelaySession& session, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    const uint8_t type = static_cast<uint8_t>(reader.ReadHeader());
    if (type == AckType)
    {
        const uint8_t kind = reader.GetU8();
        const uint32_t number = reader.GetU32();
        if (!weAreHost && fromPeer == 0 && reader.Ok() && reader.Remaining() == 0)
        {
            Unacked.erase(std::remove_if(Unacked.begin(), Unacked.end(), [kind, number](const Word& word) { return word.Kind == kind && word.Number == number; }),
                          Unacked.end());
        }
        return;
    }
    const uint8_t kind = KindOfType(type);
    if (!weAreHost || fromPeer == 0 || !kind)
    {
        return;
    }
    const uint8_t count = reader.GetU8();
    std::vector<std::pair<uint32_t, int32_t>> facts;
    for (uint8_t index = 0; index < count && index < FactsPerWord; ++index)
    {
        const uint32_t factId = reader.GetU32();
        const int32_t value = static_cast<int32_t>(reader.GetU32());
        facts.push_back({factId, value});
    }
    const uint32_t number = reader.GetU32();
    if (!reader.Ok() || reader.Remaining() != 0 || count > FactsPerWord || number == 0)
    {
        return;
    }
    nc::proto::Writer ack(static_cast<nc::proto::MsgType>(AckType));
    ack.PutU8(kind);
    ack.PutU32(number);
    session.SendGameMessage(ack.Data(), static_cast<int32_t>(fromPeer));
    if (!peerInOurWorld || !TakenWords.insert({fromPeer, kind, number}).second || !facts_db::Ready(log))
    {
        return;
    }
    int written = 0;
    for (const auto& [factId, value] : facts)
    {
        written += facts_db::WriteFact(1, factId, value) ? 1 : 0;
    }
    log("fact words: peer " + std::to_string(fromPeer) + "s " + KindName(kind) + " set " + std::to_string(written) + " of " + std::to_string(facts.size()) +
        " fact(s) here");
}
}

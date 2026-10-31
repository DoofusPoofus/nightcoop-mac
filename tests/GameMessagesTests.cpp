/*
 byte layout checks for the rebuilt game layer messages (Game envelope, PlayerAnim, PlayerAnimWant, PlayerAnimSetup,
 PlayerCustomization)
*/
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/SessionMessages.hpp"

#include <cstdio>

namespace
{
int FailedChecks = 0;

void Check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL  %s\n", what);
        ++FailedChecks;
    }
}
}

int main()
{
    using namespace nightcoop_mac::net;

    const std::vector<uint8_t> inner = {0x50, 0x4F, 0x43, 0x4E, 30, 1, 1};
    const auto envelopeBytes = WriteGameEnvelope({kEveryPeer, 2, true, inner});
    Check(envelopeBytes.size() == 5 + 4 + 4 + 1 + inner.size(), "envelope size");
    Check(envelopeBytes[5] == 0xFF && envelopeBytes[8] == 0xFF, "envelope to everyone is -1");
    GameEnvelope envelope;
    Check(ReadGameEnvelope(envelopeBytes, envelope) && envelope.ToPeer == kEveryPeer && envelope.FromPeer == 2 && envelope.Reliable &&
              envelope.Inner == inner,
          "envelope round trip");
    Check(MessageTypeOf(envelope.Inner) == 30, "inner type");

    AnimBatch batch;
    batch.Unknown00 = 7;
    batch.FullState = true;
    batch.Unknown08 = 123456789;
    batch.Unknown10 = 3;
    AnimEntry speed;
    speed.Kind = AnimKind::Float;
    speed.Name = 0x1111;
    speed.Values[0] = 4.5f;
    AnimEntry crouch;
    crouch.Kind = AnimKind::Bool;
    crouch.Name = 0x2222;
    crouch.IntValue = 1;
    AnimEntry aim;
    aim.Kind = AnimKind::Vector;
    aim.Name = 0x3333;
    aim.Values[0] = 1;
    aim.Values[3] = 1;
    AnimEntry jump;
    jump.Kind = AnimKind::Event;
    jump.Name = 0x4444;
    AnimEntry feature;
    feature.Kind = AnimKind::Feature;
    feature.Name = 0x5555;
    feature.FeatureType = 0x6666;
    feature.Fields.push_back({0x7777, {1, 0, 0, 0}});
    feature.Extra = 9;
    batch.Entries = {speed, crouch, aim, jump, feature};
    const auto batchBytes = WriteAnimBatch(batch);
    const size_t expected = 5 + 4 + 1 + 8 + 4 + 2 + (10 + 4) + (10 + 4) + (10 + 16) + 10 + (10 + 8 + 1 + (8 + 1 + 4) + 1);
    Check(batchBytes.size() == expected, "anim batch size");
    AnimBatch parsed;
    Check(ReadAnimBatch(batchBytes, parsed), "anim batch parses");
    Check(parsed.FullState && !parsed.EntriesHaveExtra && parsed.Unknown08 == 123456789 && parsed.Entries.size() == 5, "anim batch header");
    Check(parsed.Entries[0].Values[0] == 4.5f && parsed.Entries[1].IntValue == 1 && parsed.Entries[2].Values[3] == 1 &&
              parsed.Entries[3].Kind == AnimKind::Event,
          "anim batch inputs");
    Check(parsed.Entries[4].FeatureType == 0x6666 && parsed.Entries[4].Fields.size() == 1 && parsed.Entries[4].Fields[0].Bytes.size() == 4 &&
              parsed.Entries[4].Extra == 9,
          "anim batch feature");

    AnimBatch withExtra = batch;
    withExtra.EntriesHaveExtra = true;
    withExtra.Entries[0].Extra = 5;
    AnimBatch parsedExtra;
    Check(ReadAnimBatch(WriteAnimBatch(withExtra), parsedExtra) && parsedExtra.Entries[0].Extra == 5 && parsedExtra.Entries[4].Extra == 9,
          "anim batch with per entry extra bytes");

    auto broken = batchBytes;
    broken.resize(broken.size() - 3);
    AnimBatch brokenBatch;
    Check(!ReadAnimBatch(broken, brokenBatch), "a cut batch is refused");

    AnimBatch attach;
    AnimEntry attachEntry;
    attachEntry.Kind = AnimKind::Attach;
    attachEntry.Name = 1;
    attach.Entries = {attachEntry};
    AnimBatch attachParsed;
    Check(!ReadAnimBatch(WriteAnimBatch(attach), attachParsed), "an attach without a graph is refused like the dll does");

    AnimWant want;
    Check(ReadAnimWant(WriteAnimWant({true, false}), want) && want.First && !want.Second, "anim want round trip");

    const std::vector<uint8_t> setupBytes = {0x50, 0x4F, 0x43, 0x4E, 34, 1, 2, 3, 4, 5, 6, 7, 8, 2, 0};
    AnimSetup setup;
    Check(ReadAnimSetup(setupBytes, setup) && setup.Rig == 0x0807060504030201ULL && setup.SetCount == 2, "anim setup rig and count");

    PlayerCustomization look;
    look.DataVersion = 7;
    look.BodyMale = true;
    look.BrainMale = false;
    LookGroup hair;
    hair.Name = 0x1111;
    hair.Parts.push_back({0xAAAA, 0xBBBB, 0xCCCC, 2, 1});
    hair.Morphs.push_back({0xDDDD, 0xEEEE, 0, 0});
    look.Head.push_back(hair);
    look.Body.push_back({0x2222, {}, {}});
    look.Perspectives.push_back({0x3333, 0x4444, 0x5555});
    look.Tags = {0x6666, 0x7777};
    const auto lookBytes = WritePlayerCustomization(look);
    Check(lookBytes.size() == 5 + 1 + 4 + 1 + 1 + (2 + 8 + 1 + 29 + 1 + 21) + (2 + 8 + 1 + 1) + 2 + (1 + 24) + (1 + 16), "look size");
    Check(lookBytes[4] == 33 && lookBytes[5] == 1, "look is message 33 version 1");
    PlayerCustomization lookBack;
    Check(ReadPlayerCustomization(lookBytes, lookBack), "look reads back");
    Check(lookBack.DataVersion == 7 && lookBack.BodyMale && !lookBack.BrainMale, "look header fields");
    Check(lookBack.Head.size() == 1 && lookBack.Head[0].Parts.size() == 1 && lookBack.Head[0].Parts[0].Definition == 0xBBBB &&
              lookBack.Head[0].Parts[0].CensorAction == 1 && lookBack.Head[0].Morphs[0].TargetName == 0xEEEE,
          "look head group");
    Check(lookBack.Body.size() == 1 && lookBack.Arms.empty() && lookBack.Perspectives[0].Tpp == 0x5555 && lookBack.Tags[1] == 0x7777,
          "look body, arms, perspectives and tags");
    bool genderFromLook = false;
    Check(ReadCustomizationGender(lookBytes, genderFromLook) && genderFromLook, "the gender reader still reads a full look");
    auto truncated = lookBytes;
    truncated.resize(truncated.size() - 3);
    Check(!ReadPlayerCustomization(truncated, lookBack), "a cut look is refused");
    Check(LookFingerprint(lookBytes) != LookFingerprint(truncated), "look fingerprints differ");

    PlayerClothes clothes;
    clothes.Female = true;
    clothes.Items.push_back({0x0A00000012345678ull, 7, 2, 1, 0});
    clothes.Items.push_back({0x0000000522223333ull, 0, 0, 0, 1});
    clothes.Arms = 0x0000000F14BB03F3ull;
    clothes.Slots = {0x1111, 0x2222};
    const auto clothesBytes = WritePlayerClothes(clothes);
    Check(clothesBytes.size() == 5 + 1 + 2 + 2 * 16 + 8 + 2 + 2 * 8, "clothes size");
    Check(clothesBytes[4] == 31, "clothes are message 31");
    PlayerClothes clothesBack;
    Check(ReadPlayerClothes(clothesBytes, clothesBack), "clothes read back");
    Check(clothesBack.Female && clothesBack.Items.size() == 2 && clothesBack.Items[0].RngSeed == 7 && clothesBack.Items[0].UniqueCounter == 2 &&
              clothesBack.Items[1].Flags == 1 && clothesBack.Arms == clothes.Arms && clothesBack.Slots.size() == 2 && clothesBack.Slots[1] == 0x2222,
          "clothes fields");
    Check(RecordWithoutOffset(clothesBack.Items[0].Record) == 0x12345678ull, "record offset is masked off");
    auto noTail = clothesBytes;
    noTail.resize(5 + 1 + 2 + 2 * 16 + 8);
    Check(ReadPlayerClothes(noTail, clothesBack) && clothesBack.Slots.empty() && clothesBack.Items.size() == 2, "clothes without the slot tail");
    clothes.Slots.pop_back();
    Check(ReadPlayerClothes(WritePlayerClothes(clothes), clothesBack) && clothesBack.Slots.empty(), "mismatched slots are not sent");

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all game message checks passed");
    return FailedChecks ? 1 : 0;
}

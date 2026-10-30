/*
 byte layout checks for the rebuilt session messages
 sizes come from the 0.1.2 dll: 5 byte header then the fields in the order its writer puts them
*/
#include "net/SessionMessages.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

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

    const auto hello = WriteHello(0x1122334455667788ULL, "guest", 0x000300000058B49BULL, 0);
    Check(hello.size() == 5 + 4 + 8 + 2 + 5 + 8, "hello size");
    Check(hello[4] == 1, "hello type");
    Hello parsedHello;
    Check(ReadHello(hello, parsedHello) && parsedHello.Protocol == 143 && parsedHello.Nickname == "guest" &&
              parsedHello.GameVersion == 0x000300000058B49BULL,
          "hello round trip");

    const auto welcome = WriteWelcome({3, 123456});
    Check(welcome.size() == 5 + 4 + 8, "welcome size");
    Welcome parsedWelcome;
    Check(ReadWelcome(welcome, parsedWelcome) && parsedWelcome.PeerNumber == 3 && parsedWelcome.HostClockMs == 123456,
          "welcome round trip");

    PlayerState state;
    state.PeerNumber = 2;
    state.PositionX = -1300.5f;
    state.Unknown70 = 77;
    state.Unknown7C = 9;
    const auto stateBytes = WritePlayerState(state);
    Check(stateBytes.size() == 5 + 106, "player state size");
    PlayerState parsedState;
    Check(ReadPlayerState(stateBytes, parsedState) && parsedState.PositionX == -1300.5f && parsedState.Unknown70 == 77 &&
              parsedState.Unknown7C == 9 && parsedState.Bytes54[5] == 100 && parsedState.Bytes54[13] == 115,
          "player state round trip");

    std::vector<uint8_t> withoutTail(stateBytes.begin(), stateBytes.end() - 13);
    PlayerState shortState;
    Check(ReadPlayerState(withoutTail, shortState) && shortState.Unknown70 == 0 && shortState.Unknown7C == 0,
          "player state without the 13 byte tail");

    PlayerState farAway = state;
    farAway.PositionY = 200000.0f;
    Check(!PlayerStateLooksSane(farAway), "a place too far away is refused");
    PlayerState brokenYaw = state;
    brokenYaw.Yaw = NAN;
    Check(PlayerStateLooksSane(brokenYaw) && brokenYaw.Yaw == 0.0f, "a broken yaw gets zeroed");

    const std::vector<uint8_t> maleLook = {0x50, 0x4F, 0x43, 0x4E, 33, 1, 0, 0, 0, 0, 1, 0};
    bool isMale = false;
    Check(ReadCustomizationGender(maleLook, isMale) && isMale, "customization gender male");
    std::vector<uint8_t> femaleLook = maleLook;
    femaleLook[10] = 0;
    Check(ReadCustomizationGender(femaleLook, isMale) && !isMale, "customization gender female");
    std::vector<uint8_t> otherVersion = maleLook;
    otherVersion[5] = 2;
    Check(!ReadCustomizationGender(otherVersion, isMale), "customization of an unknown version is skipped");

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all session message checks passed");
    return FailedChecks ? 1 : 0;
}

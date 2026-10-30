/*
 nightcoop session messages rebuilt from the 0.1.2 dll
 Hello (1): u32 protocol | u64 client id | u16 len + nickname | u64 game version | [u64 install id]
 the game version and install id are only read when bytes are left, same as the host does
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint32_t kProtocolVersion = 143;

struct Hello
{
    uint32_t Protocol = 0;
    uint64_t ClientId = 0;
    std::string Nickname;
    uint64_t GameVersion = 0;
    uint64_t InstallId = 0;
};

std::vector<uint8_t> WriteHello(uint64_t clientId, const std::string& nickname, uint64_t gameVersion, uint64_t installId);
bool ReadHello(const std::vector<uint8_t>& message, Hello& hello);
}

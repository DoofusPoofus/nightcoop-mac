cmake_minimum_required(VERSION 3.23)
project(nightcoop_mac LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64)

set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party")
set(HUB_SOURCE "${THIRD_PARTY}/nightcoop-hub")
set(RED4EXT_SDK_DIR "${THIRD_PARTY}/RED4ext-macos/deps/red4ext.sdk" CACHE PATH "RED4ext.SDK repository root")

option(NIGHTCOOP_BUILD_LOCAL_HUB "build the public hub source as a local test hub" ON)

add_library(enet STATIC
  ${THIRD_PARTY}/enet/callbacks.c
  ${THIRD_PARTY}/enet/compress.c
  ${THIRD_PARTY}/enet/host.c
  ${THIRD_PARTY}/enet/list.c
  ${THIRD_PARTY}/enet/packet.c
  ${THIRD_PARTY}/enet/peer.c
  ${THIRD_PARTY}/enet/protocol.c
  ${THIRD_PARTY}/enet/unix.c)
target_include_directories(enet PUBLIC ${THIRD_PARTY}/enet/include)
target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_SOCKLEN_T=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1
  HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
set_target_properties(enet PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(monocypher STATIC ${HUB_SOURCE}/vendor/monocypher/monocypher.c)
target_include_directories(monocypher PUBLIC ${HUB_SOURCE}/vendor/monocypher)
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(nightcoop_shared STATIC ${HUB_SOURCE}/src/Log.cpp ${HUB_SOURCE}/src/NetCrypto.cpp)
target_include_directories(nightcoop_shared PUBLIC ${HUB_SOURCE}/src)
target_link_libraries(nightcoop_shared PUBLIC enet monocypher)
set_target_properties(nightcoop_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(nightcoop_mac_net STATIC src/net/SessionMessages.cpp src/net/GameMessages.cpp src/net/LookMessages.cpp src/net/RosterMessages.cpp src/net/VehicleMessages.cpp src/net/RelaySession.cpp)
target_include_directories(nightcoop_mac_net PUBLIC src)
target_link_libraries(nightcoop_mac_net PUBLIC nightcoop_shared)
target_compile_options(nightcoop_mac_net PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(nightcoop_mac_net PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(RED4EXT_HEADER_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(${RED4EXT_SDK_DIR} red4ext_sdk EXCLUDE_FROM_ALL)

add_library(NightCoopMac SHARED
  src/plugin/Main.cpp
  src/plugin/AnimMirror.cpp
  src/plugin/AnimSetup.cpp
  src/plugin/AnimTap.cpp
  src/plugin/BodyRecords.cpp
  src/plugin/CrashNotes.cpp
  src/plugin/Downed.cpp
  src/plugin/GameVersion.cpp
  src/plugin/Hud.cpp
  src/plugin/KeyWatch.cpp
  src/plugin/Items.cpp
  src/plugin/LookStates.cpp
  src/plugin/GameCalls.cpp
  src/plugin/GameClock.cpp
  src/plugin/PartnerBodyBuilder.cpp
  src/plugin/PartnerLook.cpp
  src/plugin/PartnerPins.cpp
  src/plugin/PartnerPlates.cpp
  src/plugin/PartnerRoster.cpp
  src/plugin/PlayerSampler.cpp
  src/plugin/RemoteBodies.cpp
  src/plugin/RttiDump.cpp
  src/plugin/SessionFile.cpp
  src/plugin/SessionFlow.cpp
  src/plugin/StateMachineBoard.cpp
  src/plugin/TestKeys.cpp
  src/plugin/Vehicles.cpp
  src/plugin/Wardrobe.cpp
  src/plugin/Weapons.cpp)
target_include_directories(NightCoopMac PRIVATE src)
target_link_libraries(NightCoopMac PRIVATE nightcoop_mac_net RED4ext::SDK "-framework CoreFoundation" "-framework AppKit" objc)
target_link_options(NightCoopMac PRIVATE -Wl,-dead_strip)
target_compile_options(NightCoopMac PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(NightCoopMac PROPERTIES PREFIX "" OUTPUT_NAME "NightCoopMac")

add_executable(nightcoop_probe tools/probe/NightCoopProbe.cpp)
target_link_libraries(nightcoop_probe PRIVATE nightcoop_mac_net)

add_executable(nightcoop_mac_tests tests/SessionMessagesTests.cpp)
target_link_libraries(nightcoop_mac_tests PRIVATE nightcoop_mac_net)

add_executable(nightcoop_mac_game_tests tests/GameMessagesTests.cpp)
target_link_libraries(nightcoop_mac_game_tests PRIVATE nightcoop_mac_net)

if(NIGHTCOOP_BUILD_LOCAL_HUB)
  add_executable(nightcoop_hub_local ${HUB_SOURCE}/src/HubServer.cpp ${HUB_SOURCE}/tools/hub/main.cpp)
  target_link_libraries(nightcoop_hub_local PRIVATE nightcoop_shared)
endif()

# nightcoop-mac

an unofficial attempt at running the [NightCoop](https://nightcoop.net) co-op mod natively on the mac version of Cyberpunk 2077.

NightCoop itself is a windows only RED4ext plugin. this is a separate native mac plugin that speaks the same network protocol, so a mac player could join windows players. it runs on [RED4ext-macos](https://github.com/codeman101/RED4ext-macos).

not affiliated with NightCoop or CD PROJEKT RED. if u are on windows just use the real NightCoop.

## status

**early. theres no co-op in the game yet.** the plugin loads and the network side works against a local hub. dont install this expecting to play with friends.

| part | state |
| --- | --- |
| plugin loads in the mac game (steam 2.5.0) | works |
| hub session create / join, encrypted links (relay only) | works against a local hub |
| Hello / Welcome / Reject / PlayerState messages (protocol 143, NightCoop 0.1.2) | works between two mac probes |
| tested against a real windows NightCoop | not yet |
| net code running inside the game | not yet |
| seeing the other player in the world | not yet |
| everything else (npcs quests vehicles dialog votes loot ...) | not yet |

NightCoop is in beta and its protocol changes with its releases, so this has to keep up with it. it targets NightCoop 0.1.2 (protocol 143) rn.

## what u need

- an apple silicon mac on macOS 15.5 or newer
- Cyberpunk 2077 2.5.0 from steam (mac version)
- Xcode command line tools, cmake 3.23+, git, python 3

## build

```sh
git clone https://github.com/DoofusPoofus/nightcoop-mac.git
cd nightcoop-mac
tools/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNIGHTCOOP_BUILD_NET=ON -DNIGHTCOOP_BUILD_LOCAL_HUB=ON
cmake --build build -j8
build/nightcoop_mac_tests
```

`fetch_deps.sh` pulls RED4ext-macos, the public [nightcoop-hub](https://github.com/Twenfys/nightcoop-hub) source (the protocol + crypto code shared by the mod and its hub) and ENet into `third_party/` at the commits this was tested with.

## install into the game

1. install RED4ext-macos with its own instructions. it re-signs the game binary once and keeps the original as `Cyberpunk2077.orig`. a steam update or "verify integrity" puts the original back
2. `tools/install_into_game.sh` (give it the game folder if its not the default steam one)
3. start the game with `launch_red4ext.sh` in the game folder

the log ends up in `red4ext/logs/` in the game folder and should have a line like:

```
NightCoopMac loading, protocol 143, mac game 2.5.0 talks to peers as exe 3.0.88.46235
```

## try the network side without the game

run a local hub and two probes:

```sh
mkdir -p localhub && cd localhub
../build/nightcoop_hub_local 47900 --sign-key-auto hub-sign.key
```

it prints a fingerprint. in another terminal:

```sh
build/nightcoop_probe host '127.0.0.1:47900#YOUR-FINGERPRINT'
build/nightcoop_probe join CODE '127.0.0.1:47900#YOUR-FINGERPRINT'
```

the host prints the guests Hello and the player states it walks.

please dont point probes at the real NightCoop hub for testing. run ur own.

## layout

- `src/plugin` the RED4ext entry points and the mac to windows game version mapping
- `src/net` the session messages
- `tools/probe` the headless client
- `tests` byte layout checks
- `tools/decompile.sh` + `tools/DumpEverything.java` ghidra headless dump used to read message formats out of a NightCoop build

## license

MIT for the code in this repo. the dependencies keep their own licenses.

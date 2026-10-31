# nightcoop-mac

unofficial try at running the [nightcoop](https://nightcoop.net) co-op mod natively on the mac version of cyberpunk 2077. nightcoop is a windows only RED4ext plugin so this is a seperate mac plugin that talks the same network protocol (nightcoop 0.1.2 protocol 143 rn). it runs on [RED4ext-macos](https://github.com/codeman101/RED4ext-macos)

not affiliated with nightcoop or cd projekt red. on windows just use the real nightcoop

## status

early. evrything below is only checked against the headless test client (`tools/probe`) on a local hub never against a real windows nightcoop or a second mac

works in a save with the probe:
- partner body with look clothes guns anims name plate n map pin
- cars n riding together both ways
- getting downed n helped up
- npcs shared both ways with anims states weapons attitude n health. the same person in ur own world becomes the owners puppet instead of a double n npcs get handed over to whoever is nearer (both ways)
- shooting n quickhacking a partners npc (hit claims n HackSync statuses) n the killer gets the reward
- quest facts n journal mirror (host to guest)
- giving items n dropped loot bags
- carried bodies n team build checks
- lifts n time of day

first cut but not tried in the game yet: doors devices dialog votes n the world handoff load

not there yet: npc hits on u do no damage (the game drops the rebuilt hit) the handoff without splicing ur own character in braindance photo mode phone police

## what u need

- apple silicon mac on macOS 15.5 or newer
- cyberpunk 2077 2.5.0 from steam (mac version)
- xcode command line tools cmake 3.23+ git python 3
- [codeware](https://github.com/codeman101/cp2077-codeware) for mac n [redscript for mac](https://github.com/jac3km4/redscript/releases) (`engine/tools/scc`)

## build

```sh
git clone https://github.com/DoofusPoofus/nightcoop-mac.git
cd nightcoop-mac
tools/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNIGHTCOOP_BUILD_NET=ON -DNIGHTCOOP_BUILD_LOCAL_HUB=ON
cmake --build build -j8
build/nightcoop_mac_tests
build/nightcoop_mac_game_tests
```

## install n play

1. install RED4ext-macos like its readme says (steam verify puts the original game binary back)
2. `tools/install_into_game.sh`
3. `tools/build_launcher.sh` builds `build/NightCoop Mac.app` with host / join. or put a `session.txt` in `red4ext/plugins/NightCoopMac/` urself:

```
host 127.0.0.1:47900#YOUR-FINGERPRINT
name macbook
```

or `join CODE 127.0.0.1:47900#YOUR-FINGERPRINT`. the log is in `red4ext/logs/` n `session-status.txt` says wats going on

## testing without the game

```sh
mkdir -p localhub && cd localhub
../build/nightcoop_hub_local 47900 --sign-key-auto hub-sign.key
```

then `build/nightcoop_probe host '127.0.0.1:47900#FINGERPRINT'` n `build/nightcoop_probe join CODE '127.0.0.1:47900#FINGERPRINT'`. the PROBE_ env switches are listed at the top of `tools/probe/NightCoopProbe.cpp`. pls dont point probes at the real nightcoop hub run ur own

## license

MIT for the code here. dependencies keep there own licenses

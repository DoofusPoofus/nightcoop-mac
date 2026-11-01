# nightcoop-mac

unofficial try at running the [nightcoop](https://nightcoop.net) co-op mod natively on the mac version of cyberpunk 2077. nightcoop is a windows only RED4ext plugin so this is a seperate mac plugin that talks the same network protocol (nightcoop 0.1.2 protocol 143 rn). it runs on [RED4ext-macos](https://github.com/codeman101/RED4ext-macos)

not affiliated with nightcoop or cd projekt red. on windows just use the real nightcoop

## status

early. evrything below is only checked against the headless test client (`tools/probe`) on a local hub never against a real windows nightcoop or a second mac

works in a save with the probe:
- partner body with look clothes guns anims name plate n map pin
- cars n riding together both ways n taking the wheel of a partners car nobody drives (VehicleHandover. the driver keeps it)
- getting downed n helped up
- npcs shared both ways with anims states weapons attitude n health. the same person in ur own world becomes the owners puppet instead of a double n npcs get handed over to whoever is nearer (both ways)
- shooting n quickhacking a partners npc (hit claims n HackSync statuses) n the killer gets the reward
- quest facts n journal mirror (host to guest) n the hosts quest rewards n calls for a guest (a guest answering or rejecting a call asks the host)
- giving items n dropped loot bags
- carried bodies n team build checks
- lifts n time of day
- doors devices n barrels (scripted toggles n blasts go out n a probes requests n blasts happen here too)
- the hosts dead npc table (NpcDead / NpcDefeat) goes out n a guests reported kills reach the host. the kill itself only works on npcs that have health (background npcs dont)
- grenade blasts push loose stuff around at the partners (no damage)
- a partners car hitting u (VehicleHit) plays the games own car hit on ur player (first cut, the knockdown isnt checked yet)
- effects a windows partner draws (PlayerFx: rockets explosive rounds slams) show up here, sending ours isnt there
- a partner opening photo mode in ur world shows a hint (PhotoTogether) n ours goes out
- tagging an npc (scanner tag) shows the tag for partners too (NpcTag)
- police shooting at a partners body starts that partners own police fight (PoliceFight). the shared wanted level n police units arent there yet

dialog votes: the message side works with a probe (votes decided at a shared time n subtitles) but pressing a choice needs a real dialog on screen

first cut but not tried in the game yet: turrets (needs a turret in a fight)

world handoff: a guest gets the hosts save with its own V spliced in (look wardrobe stat pools items stash n other character nodes) n its level attributes perks points n equipped gear get put back after it arrives n since no in game load works on mac the game restarts straight into it. when the session ends the guest goes home the same way n keeps wat it got (items level perks gear) (`handoff_restart off` / `handoff_character off` turn those off). when the host loads another save the guest follows (WorldReload 1). after the first restart later handoffs skip the dead in game loads n restart right away. the game keeps autosaving while u sit in the hosts world so ur AutoSave folders get kept aside n put back once ur home

npc hits on u only do damage thru a fallback (the game drops the rebuilt hit so the attackers weapon damage gets written straight)

the quality plus upgrade n iconic stats of ur equipped items get checked after a handoff n put back if they differ (they came along fine so far)

not there yet: the other player systems from ur own save (crafting recipes n co) visual grenade copies braindance photo poses shared police units scene gates (they need native hooks)

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

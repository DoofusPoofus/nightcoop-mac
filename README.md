# nightcoop-mac

unofficial try at running the [nightcoop](https://nightcoop.net) co-op mod natively on the mac version of cyberpunk 2077. nightcoop is a windows only RED4ext plugin so this is a seperate mac plugin that talks the same network protocol (nightcoop 0.1.2 protocol 143 rn). it runs on [RED4ext-macos](https://github.com/codeman101/RED4ext-macos)

not affiliated with nightcoop or cd projekt red. on windows just use the real nightcoop

## status

early. evrything below is only checked against the headless test client (`tools/probe`) on a local hub never against a real windows nightcoop or a second mac. stuff marked (untried) only got its messages checked

seeing each other
- partner body with look clothes guns anims name plate n map pin. hidden while a cutscene holds ur player or when it stands inside u
- getting downed n helped up. carried bodies n team build checks. photo mode hint (PhotoTogether). npc tags (NpcTag)

cars
- cars n riding together both ways. taking the wheel of a partners car nobody drives (VehicleHandover. the driver keeps it)
- the drivers radio for passengers (CarsWord). quest rides arent shared n partners sit in their own copy (VehicleSceneSeat)
- dents n flat tires both ways (VehicleDamage. the grid sits at [car + 0x538] + 0x100 on mac 2.5.0)
- a partners car hitting u (VehicleHit. the knockdown is untried) n running over a partners npc gives u the police heat (NpcCarHit)

fights n npcs
- npcs shared both ways (anims states weapons attitude health). the same person in ur world becomes the owners puppet. npcs get handed to whoever is nearer
- hit claims n HackSync quickhacks. the killer gets the reward n a partner knocking out ur npc gets its own (RewardWord 2)
- the hosts dead npc table (NpcDead / NpcDefeat) n shared corpse loot (the kill n the loot bag are untried. they need a running world)
- grenade blasts push stuff around (PlayerBlast). a windows partners effects show here (PlayerFx). police shooting at a partner starts their own police fight (PoliceFight)
- npc hits on u only do damage thru a fallback (the game drops the rebuilt hit so the attackers weapon damage gets written straight)

quests n story
- quest facts n journal mirror host to guest. a guest dropping quest items reading a document or using a device action sends its facts to the host. guests track the hosts objective
- the hosts quest rewards n phone calls (a guest answering or rejecting asks the host) n text replies picked by the host for evryone (SmsReply)
- dialog votes (message side only. pressing a choice needs a real dialog on screen)
- a windows hosts scene gates hear from us by distance. we tell it which of its scenes we play n our loading n story state (SceneGate QuestScene WorldLoad StoryHold)
- a host moved far away or calling u over gives an offer. hold PageUp (fn + up) to go (QuestTeleport StoryPull)
- scene skips fast forward the others (untried) n the host leads braindance tapes (untried)

world
- doors (automatic ones open for partner bodies) devices barrels lifts (a quest lift waits until evryone is in the cab) n time of day
- giving items n dropped loot bags. turrets are a first cut (untried. needs a fight)
- lobby hint n host kick (LobbyStart Kick)

world handoff: a guest gets the hosts save with its own V spliced in (look wardrobe stat pools items stash n other character nodes) n its level attributes perks points equipped gear n their stats get put back after it arrives. since no in game load works on mac the game restarts straight into it (later handoffs skip the dead in game loads). when the session ends the guest goes home the same way n keeps wat it got. host reloads are followed (WorldReload 1..5 WorldStatus 9). the game keeps autosaving while u sit in the hosts world so ur AutoSave folders get kept aside n put back once ur home. the mac host serves its own save too n tells a guest why it waits while saving is locked (`handoff_restart off` / `handoff_character off` turn the restart n the splice off)

not there yet: the other player systems from ur own save (crafting recipes n co) visual grenade copies braindance ghosts n clues photo poses shared police units holding our own scene gates (native hooks). the game pauses when its window loses focus (so does a mac hosts world for evryone)

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

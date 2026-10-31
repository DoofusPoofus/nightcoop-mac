# nightcoop-mac

an unofficial attempt at running the [NightCoop](https://nightcoop.net) co-op mod natively on the mac version of Cyberpunk 2077.

NightCoop itself is a windows only RED4ext plugin. this is a separate native mac plugin that speaks the same network protocol, so a mac player could join windows players. it runs on [RED4ext-macos](https://github.com/codeman101/RED4ext-macos).

not affiliated with NightCoop or CD PROJEKT RED. if u are on windows just use the real NightCoop.

## status

**early, not playable with friends yet.** a partner shows up in ur world as a body with their look, clothes, guns, anims, name, map pin and car, and u can help each other up, but all of it is only checked against a headless test client (`tools/probe`) and a local hub, never against a real windows NightCoop or a second mac. npcs, quests, doors, most of the loot and the world handoff (a guest playing in the hosts world) are not there yet, so each player still plays their own save.

| part | state |
| --- | --- |
| plugin loads in the mac game (steam 2.5.0) | works |
| hub session create / join, encrypted links (relay only) | works against a local hub |
| Hello / Welcome / Reject / PlayerState messages (protocol 143, NightCoop 0.1.2) | works between two mac probes |
| Game envelope bus (game messages wrapped, host forwards them to one peer or evryone but the sender, sender check) | works between probes and the game |
| partner anim streams (PlayerAnim / PlayerAnimWant / PlayerAnimSetup) decoded and played on their body thru the anim controller setters | works at the main menu with a probe stream |
| our own anim stream for partners (AnimTap: the anim controller setters plus the entity event queue the player state machine writes thru, unchanged values skipped) | works in a save, checked with a probe |
| tested against a real windows NightCoop | not yet |
| net code running inside the game (host / join from session.txt or the launcher) | works against a local hub |
| our own position + yaw sent as PlayerState, plus velocity, health, time dilation, camera pitch and aim yaw offset | works |
| partner body in the world (spawn / teleport / cull / despawn like nightcoops puppets) | works in a loaded save and in the menu with a probe. bodies spawn from character records (`Character.TPP_Player_Cutscene_Male/Female`, and per gender clones of `Character.Player_Puppet_Base` for the PlayerPuppet body): a template path made codeware invent a record the bodys look parts crashed on |
| partner body walks / runs / sprints with the games move policies, crouch / aim / reload stance (nightcoops npc body mode) | runs at the main menu (the menu world never moves npcs so it falls back to teleport there), not tried in the city yet |
| our crouch and sprint sent in PlayerState flags | works |
| session flow: no bodies in the main menu, bodies deleted before a session ends | works (menu checked with a probe for 2.5 min) |
| V body (cutscene template by the partners gender from PlayerCustomization) | spawns at the main menu |
| real PlayerPuppet partner body (default, `body npc` for the cutscene V): entity build hook keeps the 25 local player components off it, first person fixes, third person view, anim stream on the players own graph | spawns and plays a probe stream at the main menu (tested with `body player_without_scripts`), its script side needs redscript for mac and is not compile checked yet |
| partner look (PlayerCustomization 33: their V's face, hair, body, arms) | works in a save with a probe sending a real look (logs only, not looked at on screen yet) |
| partner clothes and arms (PlayerLook 31, nightcoops wardrobe: visible items incl transmog, arms cyberware, a third person head) | works in a save with a probe sending a real outfit (7 pieces put on, logs only) |
| our weapons in PlayerState (right / left record, shot counts from magazine drops, psm weapon states, aim / shoot / reload / silenced flags) | works in a save (`test_weapon` draws a gun and fakes shots), checked with a probe |
| partner weapons on their body (WeaponRight / WeaponLeft, npc weapon effects, Shoot anim event and muzzle flash per shot) | works in a save on both body kinds with a probe holding a gun (logs only). nightcoops native weapon sound is left out |
| time of day follows the host (GameClock 46, a guest more than 120 s off takes the hosts game time) | works both ways, checked with a probe as guest and as host |
| partner names (PlayerPresence 111 guest -> host, SessionRoster 112 host -> evryone, Hello nickname as fallback) | works, checked with a probe as guest |
| partner pins on the minimap and world map (CPO_RemotePlayerVariant at their spot, look from `NightCoopPartnerMarkers.reds`) | works in a save with a probe (the minimap made the pins look, not looked at on screen yet) |
| name plates over partner heads (name + distance in the hud cyan, scaled and faded like nightcoop) | the plate gets made and placed in a save with a probe, off screen partners stick to the screen edge (logs only, not looked at on screen yet) |
| downed and revive (PlayerRevive 47, DownedNotice 48, flag 0x100): in co-op V falls at 1 % instead of dying (`NightCoopDowned.reds` takes over the death state like nightcoop), a partner holds F 1 s next to them to help them up, T 2 s passes out | helping a downed probe up works. the fall itself is not seen yet: the test copy pauses its world without window focus and the test V has Second Heart, where the game resurrects by itself like in nightcoop |
| shared cars, first cut (VehicleSpawn / State / Despawn / Seat 60-63): the car we sit in goes out, a partners shows up as a codeware mirror on their track (teleported, not kinematic yet) with their body seated by the workspot system, and riding together works both ways (a partner in our car, us in theirs) | works both ways in a save (`test_vehicle` mounts us in a car, the probe drives one), logs only. no handover, npc / crowd / quest cars, doors, damage yet |
| lifts: partner bodies and mirror cars dont call lifts, one cab mode (LiftSync 86 kinds 2-4: the host owns evry lift, a guest press asks the host, the hosts departures start the guests copy) | the request / answer round trip works with a probe, not tried at a real lift yet. no in step start or hosts speed yet |
| doors first cut (DoorSync 88 kinds 1 and 2): the host keeps evry door (open / locked / sealed) and a guest in its world follows it. a guest tells the host wat changed at its doors and the host takes it or says no like nightcoop (no opening a locked or sealed door without a quest). newer versions win. doors get moved with the quest force unlock / lock / seal / unseal actions and OpenDoor / CloseDoor. a door doesnt close by itself while a partner stands in it (4 m flat / 3 m up). lift doors stay with the lifts. `share_doors` in session.txt turns it on without the world handoff for tests | the message round trip works between two probes and the scripts compile. not tried at a real door yet. no quest catch up holds (doors_open_behind / shut_behind / ahead_undone / own_shut) and no doors opening for partner bodies yet |
| world handoff, first cut (WorldWant 80 / WorldFile 81 / WorldStatus 82, `handoff` in session.txt): the host makes a manual save and streams sav.dat + metadata, a guest writes it as a new ManualSave-N and loads it. for now the hosts save as it is, without the guests character spliced in like nightcoop does | ask, stream (5 MB in 32 KiB chunks), hash check and the written folder work with a probe as host. the load itself did not start in the test copy (its world pauses without window focus), needs a focused test |
| partner npcs as stand ins (NpcSpawn / Snapshot / Despawn 20-22): codeware spawn from their record and look, ai components off like nightcoop, moved on their snapshot track, killed on a death, their NpcAnim stream (23) played on the copys graph after a full state (NpcAnimWant 35), the owners health %, 8 ai net states, attitude to us and hand weapons | works with a probe sharing an npc with a fake anim stream, states and a gun (logs only). the attitude write only got tried in a paused world where no attitude agent showed up |
| our npcs going to partners in our world (only the host shares, like nightcoop; `share_npcs` forces it for tests): 64 nearest within 150 m, spawn evry 625 ms, snapshots at 10 Hz with health, their anim graph writes caught by the anim tap and sent as NpcAnim evry 50 ms, snapshots with ai net states, attitudes to evry partner body, hand weapons, time dilation, incapacitated | anims work with a probe (~60 npc graphs streamed in a test save, ~2000 batches per 10 s, nothing dropped). the states part is not tried in a running world yet (in a paused one the game calls evry npc dead so nothing gets shared). no match keys, handoffs yet |
| npc combat first cut (HitClaim / HitResult 18-19): our hits on a partner npc copy go to its owner as claims (copies are immortal here), the owner takes partner claims as damage from their body and kills at 0.15 % or a lethal claim, the killer rides in NpcDespawn so only the killers copy gives a kill reward. PlayerHit 32: npc hits on a partner body go to that partner, theirs get rebuilt from the attack record with our copy of the npc as instigator and queued on our player. our npcs in combat near a body they hate get a threat on it so they fight partners too | the claim side works with a probe as owner (test_claim makes 40 % claims, the third killed it and our copy died with our reward), probe PlayerHits get queued on our player. owner claims and the real damage of a PlayerHit wait for a running world. the body threats are not tried yet. bounty and xp from partner body kills / hits are skipped on our side (CompleteBounty / AwardExperienceFromDamage wraps). no hit zones or reactions yet |
| dialogs and scenes, first cut (`NightCoopDialogs.reds`): partner bodies hide inside 0.5 m of us (back past 1.0 m) and while our scene tier is 3+, host decided choice votes (DialogBallot / Vote / Tally 74-76, VoteModeWord 249: arrows + F vote, the real F gets held while a vote is open, majority or 60 s, evry machine presses the decided option at one shared moment with fake arrow keys + F cuz no script call picks a choice), toasts for the votes, the hosts subtitles on a quiet guest (DialogLine 78) and a shared scene fast forward (SceneLineSkip 118) | the messages work between two probes (`PROBE_DIALOG`), nothing tried in the game yet. no scene ids, gather at the npc, scene gates, workspots or scene poses. `dialogs off` turns votes off, `dialogs everyone` is for probe tests |
| quests in mirror mode (QuestFacts 70 / QuestJournal 71 / QuestWant 73, `quests` in session.txt): the host lists facts table 1 thru the games native facts db evry 250 ms and sends wat changed plus journal entry states, a guest in the hosts world asks for a snapshot after evry load and writes it all over its own (SetFact by fact id, ChangeEntryStateByHash). facts evry player keeps (tutorials, wanted level, metro ...) stay out like in nightcoop | tried in a save with a probe as guest: the mac facts db checks passed (facts go by id), snapshots of 7697 facts and 23242 journal entries went out, a repeated ask got ignored. as a guest of a probe host its snapshot and deltas got written into our facts by id (own facts kept). journal apply not seen yet |
| loot, first cut (nightcoops lootsync + worldloot, 38-44, `test_loot` in session.txt): giving an item to a partner thru the hosts ledger (redscript `NightCoopGiveItem(record, quantity, peer)` like the windows native, worst case the item is lost never duped), our drops shared as LootState and partners drops mirrored here thru their body, a take asks the owner (LootTake / LootTakeResult) and a denied one gets rolled back | tried in a save with a probe: its item give (5 money) went thru the host ledger and its bag got mirrored here thru its body. our give to it and picking a mirror up are not seen working yet |
| carried bodies (PlayerCarry 92, `NightCoopBodyCarry.reds`): an npc we carry on the shoulder goes out evry 100 ms on change and evry second (the Carry anim feature from the anim tap, net id / entity id / record / look / place), the end 3 times. a partners carry gets our body of that npc mounted on their body with ragdoll off and the carry features, and 300 ms after the drop its put where they left it (hidden for good after a dispose) | tried in a save with a probe: its carried npc copy hung on its body and got put down where it dropped it (logs only) |
| team build checks (BuildStats 141, `NightCoopTeamBuild.reds`): our 11 stats from nightcoops table go to evryone, an attribute check of a dialog or scene (RPGManager.GetBuildScore) counts the best of the team in the hosts world | tried in a save with a probe: its build arrived and the team best became 20 (no attribute check tried yet) |
| game messages nobody reads yet (PlayerFx, Braindance, SmsReply ...) are checked against the sizes from the decoded dll and counted, one `drops:` line in the log evry 10 s instead of nothing | builds, checked in the tests only |
| devices, turrets and barrels, first cut (DeviceSync 139 / TurretWord 214 / DeviceBlast 130, `NightCoopDevices.reds`, `share_devices` in session.txt for tests): device actions our player finishes get copied thru rtti and sorted like nightcoop (state only, same action, host only, skill check), the host replays a guests action or forces the state, guests follow by version, destroyed devices, guest turrets ask the host who to shoot, barrels that blow go to evryone | compiles and the messages round trip, not tried in the game yet |
| everything else (braindance, photo mode, phone, police, quickhacks ...) | not yet |

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
build/nightcoop_mac_game_tests
```

`fetch_deps.sh` pulls RED4ext-macos, the public [nightcoop-hub](https://github.com/Twenfys/nightcoop-hub) source (the protocol + crypto code shared by the mod and its hub) and ENet into `third_party/` at the commits this was tested with.

## partner bodies

partner bodies need [Codeware](https://github.com/codeman101/cp2077-codeware) for mac in the game (its in the RED4ext-macos release) and [redscript for mac](https://github.com/jac3km4/redscript/releases) (`engine/tools/scc`), and they only show up in a loaded save. like nightcoops SessionFlow the plugin keeps them out of the main menu: the menu has no character look and a body copying it crashes the game. `scripts/NightCoopMac/NightCoopSessionFlow.reds` tells the plugin which session runs (codewares session events) and deletes the bodies before any session ends. nightcoop spawns its bodies with native calls that arent verified on mac, so the port uses codewares `DynamicEntitySystem` for that part. movement goes thru the same `gameTeleportationFacility.Teleport` nightcoop uses, with its thresholds (2 cm / 0.5 deg), and bodies show up within 150 m and go away past 200 m like in nightcoop.

the body is nightcoops real body (see below). `body npc` gives nightcoops fallback body instead: the cutscene V (`Character.TPP_Player_Cutscene_Female`, or `_Male` when the partners look says male), an npc puppet that tries the games own npc locomotion and gets teleported where that does nothing (it does nothing for codeware spawned npcs so far, in the menu and in the city). like nightcoop a body waits up to 5 s for the partners look before it spawns. change it in `session.txt`:

```
body npc
body_gender male
body_record Character.Judy
body_template base\characters\entities\...\some.ent
bodies off
```

### the real body (default)

nightcoops default body is a second `PlayerPuppet` from ur own template, so the partners anim stream lands on the same graph it was recorded on:

- the plugin hooks the entity build job (`EntityBuilder_ExtractComponentsJob`, verified in the mac address db) at load and, for partner bodies only, swaps the 25 components a second local player must not have (fpp camera, vehicle cameras, sound listener, hud, state machine ...) for empty turned off slots like nightcoop, and does nightcoops first person fixes (hidden parts, weapon plane slots, render plane params)
- the body gets the `NightCoopPartnerBody` tag and an `ActivateTPPRepresentationEvent` once its in the world, it moves by teleport and animates from the partners stream
- `scripts/NightCoopMac/NightCoopPartnerBodies.reds` is the script side: for tagged bodies `PlayerPuppet` attaches / detaches like a `ScriptedPuppet` and skips the local player callbacks (status effects, items, inventory, kills, death extras ...), same list as nightcoops script patches. `tools/install_into_game.sh` puts it in the plugins `Scripts` folder and the red4ext launcher compiles it, which needs [redscript for mac](https://github.com/jac3km4/redscript/releases) (`engine/tools/scc`, codeware needs it too)
- if the script side isnt compiled in, the plugin says so and uses the cutscene V body: an unpatched second PlayerPuppet would attach as a second local player. `body player_without_scripts` skips that check, only for testing at the main menu

the body walks to where the partner is with the games own npc locomotion (walk / run / sprint picked from their speed like nightcoop does), faces where they look, and crouches / aims / reloads when they do. if it gets more than 8 m behind, or its move policy does nothing for 3 s, it gets teleported instead. theres no look or clothes yet.

## install into the game

1. install RED4ext-macos with its own instructions. it re-signs the game binary once and keeps the original as `Cyberpunk2077.orig`. a steam update or "verify integrity" puts the original back
2. `tools/install_into_game.sh` (give it the game folder if its not the default steam one)
3. start the game with `launch_red4ext.sh` in the game folder

the log ends up in `red4ext/logs/` in the game folder and should have a line like:

```
NightCoopMac loading, protocol 143, mac game 2.5.0 talks to peers as exe 3.0.88.46235
```

## the launcher

`tools/build_launcher.sh` builds `build/NightCoop Mac.app`, a small window with Host / Join, the session code, the hub and ur name. it writes `session.txt` into the plugin folder and starts the game with `launch_red4ext.sh`. while the game runs it shows the session status and has a copy button for the hosting code.

"start without co-op" removes `session.txt` so the game starts with the plugin idle.

if u dont want the app, `session.txt` in `red4ext/plugins/NightCoopMac/` does the same:

```
host 127.0.0.1:47900#YOUR-FINGERPRINT
name macbook
```

or `join CODE 127.0.0.1:47900#YOUR-FINGERPRINT`. `session-status.txt` next to it says wats going on.

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
- `src/net` the session messages, the game messages (envelope + anim stream) and the relay session
- `tools/probe` the headless client (host / join / watch)
- `launcher` the launcher window
- `tests` byte layout checks (`nightcoop_mac_tests`, `nightcoop_mac_game_tests`)
- `tools/decompile.sh` + `tools/DumpEverything.java` ghidra headless dump used to read message formats out of a NightCoop build

## license

MIT for the code in this repo. the dependencies keep their own licenses.

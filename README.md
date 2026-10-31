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
| real PlayerPuppet partner body (`body player`): entity build hook keeps the 25 local player components off it, first person fixes, third person view, anim stream on the players own graph | spawns and plays a probe stream at the main menu (tested with `body player_without_scripts`), its script side needs redscript for mac and is not compile checked yet |
| partner look (PlayerCustomization 33: their V's face, hair, body, arms) | works in a save with a probe sending a real look (logs only, not looked at on screen yet) |
| partner clothes and arms (PlayerLook 31, nightcoops wardrobe: visible items incl transmog, arms cyberware, a third person head) | works in a save with a probe sending a real outfit (7 pieces put on, logs only) |
| our weapons in PlayerState (right / left record, shot counts from magazine drops, psm weapon states, aim / shoot / reload / silenced flags) | works in a save (`test_weapon` draws a gun and fakes shots), checked with a probe |
| partner weapons on their body (WeaponRight / WeaponLeft, npc weapon effects, Shoot anim event and muzzle flash per shot) | works in a save on both body kinds with a probe holding a gun (logs only). nightcoops native weapon sound is left out |
| time of day follows the host (GameClock 46, a guest more than 120 s off takes the hosts game time) | works both ways, checked with a probe as guest and as host |
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
build/nightcoop_mac_game_tests
```

`fetch_deps.sh` pulls RED4ext-macos, the public [nightcoop-hub](https://github.com/Twenfys/nightcoop-hub) source (the protocol + crypto code shared by the mod and its hub) and ENet into `third_party/` at the commits this was tested with.

## partner bodies

partner bodies need [Codeware](https://github.com/codeman101/cp2077-codeware) for mac in the game (its in the RED4ext-macos release) and [redscript for mac](https://github.com/jac3km4/redscript/releases) (`engine/tools/scc`), and they only show up in a loaded save. like nightcoops SessionFlow the plugin keeps them out of the main menu: the menu has no character look and a body copying it crashes the game. `scripts/NightCoopMac/NightCoopSessionFlow.reds` tells the plugin which session runs (codewares session events) and deletes the bodies before any session ends. nightcoop spawns its bodies with native calls that arent verified on mac, so the port uses codewares `DynamicEntitySystem` for that part. movement goes thru the same `gameTeleportationFacility.Teleport` nightcoop uses, with its thresholds (2 cm / 0.5 deg), and bodies show up within 150 m and go away past 200 m like in nightcoop.

the body is nightcoops fallback body: the cutscene V (`Character.TPP_Player_Cutscene_Female`, or `_Male` when the partners look says male). on mac it spawns as an npc puppet so it can walk and crouch with the games own npc locomotion. like nightcoop it waits up to 5 s for the partners look before it spawns. change it in `session.txt`:

```
body_gender male
body_record Character.Judy
body_template base\characters\entities\...\some.ent
bodies off
```

### the real body (`body player`)

nightcoops default body is a second `PlayerPuppet` from ur own template, so the partners anim stream lands on the same graph it was recorded on. put `body player` in `session.txt` to get that one:

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

# the full player body: wat nightcoop does and wat the mac port still needs

nightcoops default partner body is not an npc. its a second `PlayerPuppet` built from the players own template
(`ep1\characters\entities\player\player_wa_fpp_ep1.ent`, `player_ma_...`, or the base game ones without phantom liberty),
dressed in the partners look and moved by a replay of the partners first person anim graph. the mac port uses nightcoops
fallback for now: the cutscene V template (`player_wa_tpp_cutscene.ent` / `player_ma_tpp_cutscene.ent`), which spawns as an
`NPCPuppet` on mac and walks / crouches with the games npc locomotion (see `src/plugin/RemoteBodies.cpp`).

this file lists the pieces of the real thing, how nightcoop does each one and wat stands in the way on mac.

## 1. building the body before it attaches

nightcoop hooks the entity build job (`ExtractComponentsJob`, hard coded windows rva checked against the known code bytes)
and runs its BodyBuilder on the entity before it attaches:

- copies the animation setup from our own player (`entAnimationSetupExtensionComponent`, `AnimGraphResourceContainer`, `player_tpp_head`)
- keeps these components off a remote PlayerPuppet: `vehicleCameraManagerComponent`, `vehicleTPPCameraComponent`,
  `entSoundListenerComponent`, `WidgetHudComponent`, `WidgetMenuComponent`, `gamestateMachineComponent`,
  `gamePlayerCommandConsumerComponent`, `vehicleGarageComponent`, `gameComponentsStateSaveComponent`, `gamePlayerTierComponent`,
  `gamePlayerMappinComponent`, `gameBreachFinderComponent`, `gameTargetingActivatorComponent`, `gameVisionActivatorComponent`,
  `gameScanningActivatorComponent`, `InspectionComponent`, `DisarmComponent`, `DEBUG_VisualizerComponent`,
  `gameAudioEmitterComponent`, `senseSensorObjectComponent`, `WeaponPositionComponent`, `vehicleVisualCustomizationComponent`
- adds `senseVisibleObjectComponent` (npcs see the partner), `HitPhysicalQueryMesh`, a `gameuiCharacterCustomizationBodyController`
  (`NightCoopBodyController`), two blocker capsules and an item pusher

**mac:** nightcoop resolves its hook target thru RED4ext address hash `489494088`, which is `EntityBuilder_ExtractComponentsJob`.
the mac 2.5.0 address db has it **verified** (`1:0x19B74B0`) and the mac ports of codeware and archivexl already hook it, so this
part can be ported for real. codeware for mac declares the job as `void(EntityBuilderJobParams* params, void*)` with
`params->entityBuilder` an `EntityBuilder*` whose `components` (`DynArray<Handle<IComponent>>`) sit at `+0xC0`, `entity` at `+0xB0`
and `entityTemplate` at `+0x90` (`src/Red/EntityBuilder.hpp` in codeman101/cp2077-codeware). the job runs on worker threads, and
the players own entity is built from the same template, so a partner body has to be picked by its entity id, not its template.
turning components off after attach (`entIComponent.Toggle`) would be too late for the camera, sound listener and state machine.

**done on mac** (`src/plugin/PartnerBodyBuilder.cpp`, `body player` in session.txt): the hook runs before the job like
nightcoops and codewares, picks partner bodies by the entity id from their spawn request, checks the root is a PlayerPuppet and:
- swaps the 25 kept off classes (nightcoops table at `0x1810a9210`, 3 more than listed above: `gameFPPCameraComponent`,
  `entTriggerActivatorComponent`, `gameinteractionsComponent`) for an empty turned off `entSlotComponent` named
  `NightCoopRemoved_<name>` with the same id, the way nightcoop does it so the package indices stay put
- `gameuiCharacterCustomizationBodyPartsController.isHiddenInFpp` off, `gameAttachmentSlots.animParams` with function 0 lose
  their param name (no more moving items to the first person weapon plane), and `entAnimatedComponent.animParameters` named
  `renderPlane` (and two more hashes `0x44b42b9359b79f22`, `0x13e09e8b9d7d102f`) get track `0xf6c0a5626de68fc2` and default 0
- main menu test with a probe: 119 objects, 27 components kept off, 3 parts shown, 5 slot options and 5 render plane params
  fixed, the body comes in as a `PlayerPuppet`, the tpp event gets queued and the probe stream plays on it (116 played, 0 failed)
- the body uses the template family our own player came from (codewares `Entity.GetTemplatePath`: ep1 or base, by the partners gender)

still open here: copying our live players anim setup (`rig`, `graph`, `animTags`, `animations` of the `root` animated
component, after clearing the template extensions), the impostor flag (`+0xB1`, not in rtti), the added sense / hit / body
controller components, nightcoops `SceneItemPlanes` / `SceneRenderPlanes` on items attached later and the `SetPlaneSetting` event.

**the crash a moment after a body spawned** (found with `src/plugin/CrashNotes.cpp`, which names the game objects in the
crashing threads registers): the bodys character customization parts (genitals, hairstyle, feet controllers ...) ask the body
for its record and read it as a character record. spawning from a template path makes codeware invent a `SpawnableObject`
record (`TweakDBID(hash, 1)`), so the lookup comes back empty and the game locks a null object. nightcoop spawns with a
character record (`Character.TPP_Player_Cutscene_Male/Female`, `Character.Player_Puppet_Base`), so does the mac port now.
writing the record into the spawn request or onto the root object from the build hook is too late, the stub brings the
invented one back. so like nightcoop (its `_g` records) `src/plugin/BodyRecords.cpp` clones `Character.Player_Puppet_Base` per
template into `Character.Player_Puppet_Base_g<template hash>`: 87 flats copied (found by the record classes getter names),
its own `entityTemplatePath`, the genders of the cutscene V record of that gender, then `TweakDB_CreateRecord` (verified on mac).
the PlayerPuppet body spawns from that record: `player_wa_fpp_ep1.ent` / `player_ma_fpp_ep1.ent` (or the base ones, the family
our own player came from), checked at the menu for both genders.

## partner look (done on mac, checked in a save with a probe)

nightcoops PartnerLook, rebuilt: `src/net/LookMessages.cpp` reads and writes the full PlayerCustomization (33) (gender, head /
body / arms groups with their parts and morphs, perspectives, tags, all CName hashes), `src/plugin/LookStates.cpp` reads our
own look out of the customization systems state (+0x41 / +0x44 / +0x45 / +0xC0 and the arrays at +0x70..+0xB0, walked thru the
rtti types of `gameuiCharacterCustomizationPreset`) and builds a fresh state per partner, `src/plugin/PartnerLook.cpp` hooks
the systems `GetState(isMale)` (vtable 0x1f8, game+0x4bc7228) and `GetState()` (0x1f0, game+0x4bc6098), both checked by their
code bytes. a partner body is found by the callers callee saved registers (an asm entry hands x19..x28 to the detour; the part
or entity of a partner body is in one of them), the menu only look part function (game+0x4bacda8) sets a thread local scope,
and an other gender call outside any scope goes to the only partner of that gender like nightcoops fallback. in a save our look
read back as 11 head, 5 body, 34 arms groups (3880 bytes) and a body bound to it got its own state for evry request.
not done: DetachUpdaters (our own look changes may still repaint partners), respawn on a look change, clothes (PlayerLook 31,
the wardrobe module), the 4 extra scope hooks nightcoop has for items. tests can load a save alone with `auto_continue`.

## partner clothes (done on mac, checked in a save with a probe)

nightcoops wardrobe module, rebuilt: `src/net/LookMessages.cpp` has PlayerLook (31) (gender, up to 64 ItemIDs, arms, placement
slots), `src/plugin/Wardrobe.cpp` reads our visible items per equipment area thru EquipmentSystemPlayerData (hidden slots
skipped, transmog included, underwear by censorship) and dresses a body with GivePreviewItemByItemID + CreatePreviewItemID +
AddItemToSlot on the scene plane, arms with GiveItem + AddItemToSlot, then &FPP -> &TPP on the tpp slots. no hooks, all rtti.
on our PlayerPuppet body the tpp view event does not swap the first person head (`Items.PlayerFppHead`, faceless) in TppHead,
so after the 8 s head wait the wardrobe takes it off and puts `Items.PlayerMaTppHead` / `PlayerWaTppHead` on, like nightcoop
does for bodies without a tpp view. test: 6 pieces plus arms plus head, the head came up as
`h0_000_pma__basehead__01_ca_pale_00_warm_ivory`. not done: arms cyberware weapons (arms off while one is drawn), EquipmentEx
outfit slots, defaults for items with no local record, why the tpp view event does nothing on our body.

## 2. script callbacks of PlayerPuppet

about 30 scripted callbacks are cut for remote bodies so they dont act like a second local player: `OnStatusEffectApplied`,
`OnStatusEffectRemoved`, `OnTargetNeutraliziedEvent`, `UpdateLootInteraction`, `OnItemAddedToSlot`, `OnItemAddedToInventory`,
`OnBeingTargetByLaserSight`, `OnCarHitPlayer`, `OnDamageReceived`, `SendDamageRequestToPreventionSystem`,
`CheckIfPreventionShouldReact`, the death path, and ~30 ai lookups get pointed at the nearest player.

**mac:** this part can be done without native hooks: redscript `@wrapMethod(PlayerPuppet)` wrappers that skip when the
puppet is a partner body. needs redscript for mac (`engine/tools/scc`), which the RED4ext-macos launcher already runs.

**written, not compile checked yet** (`scripts/NightCoopMac/NightCoopPartnerBodies.reds`): nightcoops table (the one with
OnRequestComponents / OnGameAttached / OnDetach and the rest, built in `FUN_18021f3c0`) says per function wat a remote
body does instead: `OnRequestComponents` = ScriptedPuppets + `targeting_primary` + `environmentDamageReceiver` (the vanilla one
asks for the vehicle cameras as mandatory), `OnGameAttached` = ScriptedPuppets + `UpdateSecondaryVisibilityOffset(false)`,
`EnableCombatVisibilityDistances(false)`, `SetSenseObjectType(Player)` (no PlayerAttachRequest, input, facts, ui, stat pools),
`OnDetach` = ScriptedPuppets, status effects / being noticed / laser sight / car hit do nothing, death and item slot callbacks go
to ScriptedPuppet, the kill / inventory / item appearance / encumbrance ones are skipped, `UpdateLootInteraction` is skipped.
partner bodies are found by codewares `DynamicEntitySystem.IsTagged(id, n"NightCoopPartnerBody")`, no native of ours needed.
the plugin checks for the `NightCoopPartnerBodies` script class and keeps to the cutscene V body without it. the police /
prevention / npc targeting patches of the same table are about gameplay, not the body, and are not here.

## 3. third person view

`gameActivateTPPRepresentationEvent` queued on `gameTPPRepresentationComponent` once the body is ready. script reachable, easy.

**done on mac:** queued with `Entity.QueueEvent` when the body comes into the world.

## 4. the anim mirror

wire format (read out of the dll, see the top of `src/net/GameMessages.hpp`): game messages ride in `Game` (25) envelopes,
`PlayerAnim` (29) batches hold slot / kind / name hash / value entries for features, floats, ints, bools, vectors, quaternions,
events, wrapper weights and graph attach / detach.


- **AnimTap** hooks the input writes of `entAnimationControllerComponent` on our players graphs (`player_ranged`, `player_melee`,
  `player_vehicle`, `player_locomotion`, `player_inspection`, `player_interaction`, `player_carrying`, `player_adhoc`) and records
  features, floats, ints, bools, vectors, quaternions, events, wrappers and attach / detach
- they go out as `PlayerAnim` (29) batches, a body that fell behind asks for the full state with `PlayerAnimWant` (30), and the
  rig + animation sets go in `PlayerAnimSetup` (34)
- **AnimMirror** replays them on the partners body, which runs the players own first person graph
- a **pose fix** hook on the graph write back (anim job threads) bends the first person pose into a third person one, and npc clip
  layers (upper body by weapon class, legs, melee) cover wat the first person graph doesnt do

**mac:** the replay side looks script reachable (`AnimationControllerComponent.SetInputFloat / SetInputBool / SetInputInt /
SetInputVector / PushEvent / ApplyFeature`), but the capture side and the pose fix are native hooks, and the wire format of
`PlayerAnim` / `PlayerAnimSetup` still has to be read out of the dll.

**done on mac:** the receive side. `src/net/GameMessages.cpp` reads and writes the batches, `src/plugin/AnimMirror.cpp` plays
them on the partners body thru `ScriptedPuppet.GetAnimationControllerComponent()` and the controllers `ApplyFeature`,
`SetInputFloat / Int / Bool / Vector / Quaternion` and `PushEvent`, asks a new body for the full state with `PlayerAnimWant` and logs
the stream like nightcoop. wrapper weights and attach / detach (kinds 7 to 9) only get counted, they have no script reachable setter.
on the cutscene V body (an npc graph) most player graph inputs dont do much yet, they do their real job once the body runs the
players own graph (part 1 above).

**not done:** our own stream. the setters above only write, theres nothing to read a graphs inputs back, and catching the
writes the way AnimTap does means hooking the controllers native input writes (the game's c++ writes most of them, not scripts).
AnimTap hooks them by RED4ext address hash, and none of the 11 is in the mac 2.5.0 address db:

| what | hash |
| --- | --- |
| feature | 316482401 (0x12dd2361) |
| float | 3216184875 (0xbfb3162b) |
| int | 3215201830 (0xbfa41626) |
| bool | 3598063243 (0xd676168b) |
| vector | 3828226594 (0xe42e1a22) |
| quaternion | 953293729 (0x38d21ba1) |
| event | 3247117892 (0xc18b1644) |
| wrapper | 1245585201 (0x4a3e1f31) |
| attach | 2783782033 (0xa5ed2491) |
| detach | 4105184598 (0xf4b02556) |
| wrapper event path | 808393639 (0x302f1ba7) |

they have to be found in the mac binary and verified (with the windows exe next to it, the way the RED4ext.SDK-macos address
db was built) before our own stream can be recorded. until then windows partners get our PlayerState but no PlayerAnim from us,
and a PlayerAnimWant to us gets logged.

## order that makes sense from here

1. ~~the real PlayerPuppet body~~ (done, see part 1 and 3)
2. ~~redscript wrappers for the PlayerPuppet callbacks~~ (written, needs a compile with redscript for mac and a run in the city)
3. the rest of the body builder (anim setup copy from our player, item planes, senses / hits) and the partners look and gear
4. find and verify the 11 AnimTap targets in the mac binary, then record our own stream (windows partners see mac players animate)

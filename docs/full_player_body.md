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

**mac:** theres no verified address for that build job in the mac address db. turning components off after attach
(`entIComponent.Toggle`) is too late for the camera, sound listener and state machine. this needs the job found in the mac
binary and verified the way `docs/PORTING_A_PLUGIN.md` of RED4ext.SDK-macos asks, before anything can hook it.

## 2. script callbacks of PlayerPuppet

about 30 scripted callbacks are cut for remote bodies so they dont act like a second local player: `OnStatusEffectApplied`,
`OnStatusEffectRemoved`, `OnTargetNeutraliziedEvent`, `UpdateLootInteraction`, `OnItemAddedToSlot`, `OnItemAddedToInventory`,
`OnBeingTargetByLaserSight`, `OnCarHitPlayer`, `OnDamageReceived`, `SendDamageRequestToPreventionSystem`,
`CheckIfPreventionShouldReact`, the death path, and ~30 ai lookups get pointed at the nearest player.

**mac:** this part can be done without native hooks: redscript `@wrapMethod(PlayerPuppet)` wrappers that skip when the
puppet is a partner body, plus a native from our plugin that says which entities are partner bodies. needs redscript for mac
(`engine/tools/scc`), which the RED4ext-macos launcher already runs.

## 3. third person view

`gameActivateTPPRepresentationEvent` queued on `gameTPPRepresentationComponent` once the body is ready. script reachable, easy.

## 4. the anim mirror

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

## order that makes sense

1. decode `PlayerAnim` (29) / `PlayerAnimWant` (30) / `PlayerAnimSetup` (34) from the dll and replay windows partners streams on
   the cutscene V body thru the script reachable setters (mac players see windows partners animate)
2. redscript wrappers for the PlayerPuppet callbacks and the TPP event
3. find and verify the build job and the anim controller input writes in the mac binary, then the real PlayerPuppet body and
   our own AnimTap (windows partners see mac players animate)

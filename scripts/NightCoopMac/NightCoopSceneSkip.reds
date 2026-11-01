// scene skip the script half of the plugins SceneSkip (nightcoops sceneskip): why our player may not skip now (0 may 1 combat
// 5 in a car) and a fast forward of the scene that holds our player for a partner who skipped

public class NightCoopSceneSkip extends ScriptableService {
  public func WhyNot() -> Int32 {
    let player = GetPlayer(GetGameInstance()) as PlayerPuppet;
    if !IsDefined(player) {
      return 0;
    }
    if player.IsInCombat() {
      return 1;
    }
    if IsDefined(player.GetMountedVehicle()) {
      return 5;
    }
    return 0;
  }

  public func FastForward(on: Bool) -> Bool {
    let player = GetPlayer(GetGameInstance());
    let scenes = GameInstance.GetSceneSystem(GetGameInstance());
    if !IsDefined(player) || !IsDefined(scenes) {
      return false;
    }
    let sceneInterface = scenes.GetScriptInterface();
    if !IsDefined(sceneInterface) {
      return false;
    }
    if !on {
      if sceneInterface.IsFastForwardingActive(scnFastForwardMode.Default) {
        sceneInterface.FastForwardingDeactivate();
      }
      return true;
    }
    if !sceneInterface.IsEntityInScene(player.GetEntityID()) || !sceneInterface.IsFastForwardingAllowed(scnFastForwardMode.Default) ||
      sceneInterface.IsFastForwardingActive(scnFastForwardMode.Default) {
      return false;
    }
    sceneInterface.FastForwardingActivate(scnFastForwardMode.Default);
    return true;
  }
}

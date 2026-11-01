// cutscenes the script half of the plugins cutscene body hiding (nightcoops SetBodyHidden): while a scene holds our player
// and its camera the partner bodies get their visual components turned off so two Vs never stand in one spot of the scene

public class NightCoopCutscenes extends ScriptableService {
  public func InCutscene() -> Bool {
    let player = GetPlayer(GetGameInstance());
    let scenes = GameInstance.GetSceneSystem(GetGameInstance());
    if !IsDefined(player) || !IsDefined(scenes) {
      return false;
    }
    let sceneInterface = scenes.GetScriptInterface();
    return IsDefined(sceneInterface) && sceneInterface.IsEntityInScene(player.GetEntityID()) && sceneInterface.GetSceneSystemCameraControlEnabled();
  }

  public func SetBodyHidden(body: ref<GameObject>, hidden: Bool) -> Int32 {
    if !IsDefined(body) {
      return 0;
    }
    let toggled = 0;
    for component in body.GetComponents() {
      if component.IsA(n"entIVisualComponent") && NotEquals(component.IsEnabled(), !hidden) {
        component.Toggle(!hidden);
        toggled += 1;
      }
    }
    return toggled;
  }
}

// scene skip the script half of the plugins SceneSkip (nightcoops sceneskip): why our player may not skip now (0 may 1 combat
// 5 in a car) a fast forward of the scene that holds our player for a partner who skipped and whether our player holds the
// skip right now (the games fast forward automaton state Active) not counting the fast forwards we run for a partner

public class NightCoopSceneSkip extends ScriptableService {
  private let m_pressing: Bool;
  private let m_applying: Bool;

  public func NotePress(on: Bool) {
    if !this.m_applying {
      this.m_pressing = on;
    }
  }

  public func Pressing() -> Bool {
    return this.m_pressing;
  }

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
      this.m_applying = false;
      return true;
    }
    if !sceneInterface.IsEntityInScene(player.GetEntityID()) || !sceneInterface.IsFastForwardingAllowed(scnFastForwardMode.Default) ||
      sceneInterface.IsFastForwardingActive(scnFastForwardMode.Default) {
      return false;
    }
    this.m_applying = true;
    sceneInterface.FastForwardingActivate(scnFastForwardMode.Default);
    return true;
  }
}

@wrapMethod(FastForwardActiveEvents)
public func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) {
  wrappedMethod(stateContext, scriptInterface);
  let skip = GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSceneSkip") as NightCoopSceneSkip;
  if IsDefined(skip) {
    skip.NotePress(true);
  }
}

@wrapMethod(FastForwardActiveEvents)
public func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) {
  wrappedMethod(stateContext, scriptInterface);
  let skip = GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSceneSkip") as NightCoopSceneSkip;
  if IsDefined(skip) {
    skip.NotePress(false);
  }
}

// the script half of the mac sessionflow, like nightcoops SessionFlow it decides when partner bodies may exist
// codeware tells evry game session start and end. a session that starts with IsPreGame is the main menu (or the intro
// world before it): no bodies there, the menu has no character look and a V body copying it crashes the game.
// right before any session ends all tagged partner bodies get deleted so none is left when the world goes
// the plugin gets this service from codewares container and reads its three fields thru rtti, script statics are not in rtti
// testing only: when the plugin sets autoContinue (session.txt auto_continue) the main menu loads the newest save once by
// itself (LoadLastCheckpoint, wat its Continue button does), so a test copy of the game gets into a
// save with nobody at the keyboard (the plugin presses a key on the press any key screen, see SessionFlow)
// menuSteps keeps the menu steps seen as decimal digits (1 start screen, 2 saves listed, 3 no saves, 4 load asked)

public class NightCoopSessionFlow extends ScriptableService {
  private let m_sessionRunning: Bool;
  private let m_preGame: Bool;
  private let m_sessionsStarted: Int32;
  private let m_autoContinue: Bool;
  private let m_autoContinueDone: Bool;
  private let m_menuSteps: Int32;

  private cb func OnLoad() {
    let callbacks = GameInstance.GetCallbackSystem();
    callbacks.RegisterCallback(n"Session/Start", this, n"OnSessionStart");
    callbacks.RegisterCallback(n"Session/BeforeEnd", this, n"OnSessionBeforeEnd");
  }

  private cb func OnSessionStart(event: ref<GameSessionEvent>) {
    this.m_sessionRunning = true;
    this.m_preGame = event.IsPreGame();
    this.m_sessionsStarted += 1;
  }

  private cb func OnSessionBeforeEnd(event: ref<GameSessionEvent>) {
    this.m_sessionRunning = false;
    let dynamicEntities = GameInstance.GetDynamicEntitySystem();
    if IsDefined(dynamicEntities) {
      dynamicEntities.DeleteTagged(n"NightCoopPartnerBody");
    }
  }

  public static func NoteMenuStep(step: Int32) {
    let flow = GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSessionFlow") as NightCoopSessionFlow;
    if IsDefined(flow) {
      flow.m_menuSteps = flow.m_menuSteps * 10 + step;
    }
  }

  public static func AutoContinueWanted() -> Bool {
    let flow = GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSessionFlow") as NightCoopSessionFlow;
    return IsDefined(flow) && flow.m_autoContinue && !flow.m_autoContinueDone;
  }

  public static func TakeAutoContinue() -> Bool {
    let flow = GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSessionFlow") as NightCoopSessionFlow;
    if !IsDefined(flow) || !flow.m_autoContinue || flow.m_autoContinueDone {
      return false;
    }
    flow.m_autoContinueDone = true;
    return true;
  }
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnSavesForLoadReady(saves: array<String>) -> Bool {
  let handled = wrappedMethod(saves);
  NightCoopSessionFlow.NoteMenuStep(ArraySize(saves) > 0 ? 2 : 3);
  if ArraySize(saves) > 0 && NightCoopSessionFlow.TakeAutoContinue() {
    NightCoopSessionFlow.NoteMenuStep(4);
    this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
  }
  return handled;
}

@wrapMethod(MenuScenario_PreGameSubMenu)
protected cb func OnSwitchToEngagementScreen() -> Bool {
  NightCoopSessionFlow.NoteMenuStep(1);
  return wrappedMethod();
}


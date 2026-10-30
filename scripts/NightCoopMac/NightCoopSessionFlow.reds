// the script half of the mac sessionflow, like nightcoops SessionFlow it decides when partner bodies may exist
// codeware tells evry game session start and end. a session that starts with IsPreGame is the main menu (or the intro
// world before it): no bodies there, the menu has no character look and a V body copying it crashes the game.
// right before any session ends all tagged partner bodies get deleted so none is left when the world goes
// the plugin gets this service from codewares container and reads its three fields thru rtti, script statics are not in rtti

public class NightCoopSessionFlow extends ScriptableService {
  private let m_sessionRunning: Bool;
  private let m_preGame: Bool;
  private let m_sessionsStarted: Int32;

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
}

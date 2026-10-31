// downed and revive, the script half of the plugins Downed module (nightcoops downed)
// while falls are in force (the plugin says so while a partner is in our world) our player is immortal (NightCoopDowned)
// and at the 1 % floor it falls instead of dying: the death state is entered with resurrection allowed and the player lies
// as downed (invulnerable NightCoopDownedShield, npcs see a dead body, no phone) until the plugin asks it to rise (a partner
// helped us up, we held T, or falls are not in force any more). the games own resurrect state stands it up again.
// the game keeps its real death when it wants one: ForceKill, ForcePreventResurrect or our own Second Heart
// the plugin calls PluginTick evry 100 ms and gets the state back: 0 up, 1 down, 2 rising

public class NightCoopDowned extends ScriptableService {
  private let m_fallsInForce: Bool;
  private let m_immortalOn: Bool;
  private let m_state: Int32;
  private let m_riseAsked: Bool;
  private let m_risingTicks: Int32;
  private let m_falls: Int32;
  private let m_enterChecks: Int32;
  private let m_enterTrue: Int32;

  public static func Get() -> ref<NightCoopDowned> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopDowned") as NightCoopDowned;
  }

  public func HealthPercent(player: ref<GameObject>) -> Float {
    return GameInstance.GetStatPoolsSystem(player.GetGame()).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
  }

  public func TakesTheFall(player: ref<GameObject>) -> Bool {
    if !this.m_fallsInForce || !IsDefined(player) {
      return false;
    }
    let game = player.GetGame();
    let id = player.GetEntityID();
    if GameInstance.GetStatusEffectSystem(game).HasStatusEffect(id, t"BaseStatusEffect.ForceKill") {
      return false;
    }
    let stats = GameInstance.GetStatsSystem(game);
    if stats.GetStatValue(Cast<StatsObjectID>(id), gamedataStatType.ForcePreventResurrect) != 0.0 {
      return false;
    }
    return stats.GetStatValue(Cast<StatsObjectID>(id), gamedataStatType.HasSecondHeart) <= 0.0;
  }

  public func ShouldEnterDeath(player: ref<GameObject>) -> Bool {
    return this.m_state == 0 && this.TakesTheFall(player) && this.HealthPercent(player) <= 1.1;
  }

  public func GoDown(player: ref<GameObject>) {
    if this.m_state != 0 {
      return;
    }
    this.m_state = 1;
    this.m_riseAsked = false;
    this.m_falls += 1;
    let puppet = player as ScriptedPuppet;
    if IsDefined(puppet) {
      puppet.SetSenseObjectType(gamedataSenseObjectType.Deadbody);
    }
    GameInstance.GetGodModeSystem(player.GetGame()).AddGodMode(player.GetEntityID(), gameGodModeType.Invulnerable, n"NightCoopDownedShield");
    StatusEffectHelper.ApplyStatusEffect(player, t"GameplayRestriction.NoPhone");
  }

  public func DecideRise() -> Bool {
    if this.m_state == 2 {
      return true;
    }
    if this.m_state == 1 && this.m_riseAsked {
      this.m_state = 2;
      this.m_riseAsked = false;
      this.m_risingTicks = 0;
      return true;
    }
    return false;
  }

  public func IsRising() -> Bool {
    return this.m_state == 2;
  }

  public func IsLying() -> Bool {
    return this.m_state == 1;
  }

  public func Describe() -> String {
    let player = GameInstance.GetPlayerSystem(GetGameInstance()).GetLocalPlayerMainGameObject();
    if !IsDefined(player) {
      return "no player";
    }
    let vitals = (player as PlayerPuppet).GetPlayerStateMachineBlackboard().GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vitals);
    return "state " + IntToString(this.m_state) + ", falls in force " + BoolToString(this.m_fallsInForce) + ", health " +
      FloatToString(this.HealthPercent(player)) + " %, takes the fall " + BoolToString(this.TakesTheFall(player)) + ", immortal " +
      BoolToString(GameInstance.GetGodModeSystem(player.GetGame()).HasGodMode(player.GetEntityID(), gameGodModeType.Immortal)) +
      ", vitals " + IntToString(vitals) + ", entered " + IntToString(this.m_enterChecks) + " / " + IntToString(this.m_enterTrue) +
      ", force kill " + BoolToString(GameInstance.GetStatusEffectSystem(player.GetGame()).HasStatusEffect(player.GetEntityID(), t"BaseStatusEffect.ForceKill")) +
      ", prevent resurrect " + FloatToString(GameInstance.GetStatsSystem(player.GetGame()).GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatType.ForcePreventResurrect)) +
      ", second heart " + FloatToString(GameInstance.GetStatsSystem(player.GetGame()).GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatType.HasSecondHeart)) +
      ", invulnerable " + BoolToString(GameInstance.GetGodModeSystem(player.GetGame()).HasGodMode(player.GetEntityID(), gameGodModeType.Invulnerable));
  }

  public func TestDropHealth() {
    let player = GameInstance.GetPlayerSystem(GetGameInstance()).GetLocalPlayerMainGameObject();
    if IsDefined(player) {
      GameInstance.GetStatPoolsSystem(player.GetGame()).RequestChangingStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, -99.0, player, false, true, false);
    }
  }

  public func NoteEnterCheck(answer: Bool) {
    this.m_enterChecks += 1;
    if answer {
      this.m_enterTrue += 1;
    }
  }

  public func AskRise() {
    if this.m_state == 1 {
      this.m_riseAsked = true;
    }
  }

  public func PluginTick(fallsInForce: Bool) -> Int32 {
    let player = GameInstance.GetPlayerSystem(GetGameInstance()).GetLocalPlayerMainGameObject() as PlayerPuppet;
    if !IsDefined(player) {
      return this.m_state;
    }
    let gods = GameInstance.GetGodModeSystem(player.GetGame());
    if fallsInForce && !this.m_immortalOn {
      gods.AddGodMode(player.GetEntityID(), gameGodModeType.Immortal, n"NightCoopDowned");
      this.m_immortalOn = true;
    } else {
      if !fallsInForce && this.m_immortalOn {
        gods.RemoveGodMode(player.GetEntityID(), gameGodModeType.Immortal, n"NightCoopDowned");
        this.m_immortalOn = false;
      }
    }
    this.m_fallsInForce = fallsInForce;
    if this.m_state == 1 && !fallsInForce {
      this.m_riseAsked = true;
    }
    if this.m_state == 2 {
      this.m_risingTicks += 1;
      let vitals = player.GetPlayerStateMachineBlackboard().GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vitals);
      let dead = vitals == EnumInt(gamePSMVitals.Dead);
      if !dead && (this.m_risingTicks > 15 && this.HealthPercent(player) > 5.5 || this.m_risingTicks > 50) {
        this.StandUp(player);
      }
    }
    return this.m_state;
  }

  private func StandUp(player: ref<PlayerPuppet>) {
    this.m_state = 0;
    this.m_risingTicks = 0;
    let game = player.GetGame();
    GameInstance.GetGodModeSystem(game).RemoveGodMode(player.GetEntityID(), gameGodModeType.Invulnerable, n"NightCoopDownedShield");
    GameInstance.GetStatusEffectSystem(game).RemoveStatusEffect(player.GetEntityID(), t"GameplayRestriction.NoPhone", 1u);
    player.SetSenseObjectType(gamedataSenseObjectType.Player);
  }
}

@wrapMethod(DeathDecisions)
protected const func EnterCondition(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  let downed = NightCoopDowned.Get();
  if IsDefined(downed) {
    let falls = downed.ShouldEnterDeath(scriptInterface.executionOwner);
    downed.NoteEnterCheck(falls);
    if falls {
      return true;
    }
  }
  return wrappedMethod(stateContext, scriptInterface);
}

@wrapMethod(HighLevelTransition)
protected final func SetIsResurrectionAllowedBasedOnState(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) {
  let downed = NightCoopDowned.Get();
  if IsDefined(downed) && (downed.IsLying() || downed.TakesTheFall(scriptInterface.executionOwner)) {
    stateContext.SetPermanentBoolParameter(n"isResurrectionAllowed", true, true);
    downed.GoDown(scriptInterface.executionOwner);
    return;
  }
  wrappedMethod(stateContext, scriptInterface);
}

@wrapMethod(DeathDecisionsWithResurrection)
protected const func ToResurrect(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  let downed = NightCoopDowned.Get();
  if IsDefined(downed) && (downed.IsLying() || downed.IsRising()) {
    return downed.DecideRise();
  }
  return wrappedMethod(stateContext, scriptInterface);
}

@wrapMethod(ResurrectDecisions)
protected const func ToDeath(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  let downed = NightCoopDowned.Get();
  if IsDefined(downed) && downed.IsRising() {
    return false;
  }
  return wrappedMethod(stateContext, scriptInterface);
}

// short lines on the hud for the plugin (nightcoops hud Show): a SimpleScreenMessage on UI_Notifications.OnscreenMessage
public class NightCoopHud extends ScriptableService {
  public func Show(text: String, seconds: Float) {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = seconds;
    message.message = text;
    let notifications = GetAllBlackboardDefs().UI_Notifications;
    GameInstance.GetBlackboardSystem(GetGameInstance()).Get(notifications).SetVariant(notifications.OnscreenMessage, ToVariant(message), true);
  }
}

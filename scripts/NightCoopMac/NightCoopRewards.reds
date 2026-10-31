// rewards the script half of the plugins Rewards module (nightcoops rewards reward_quests_shared)
// the host notes evry quest reward its player gets (PlayerPuppet.OnRewardEvent) and the plugin sends them as RewardWord kind 1
// a guest in the hosts world skips its own quest rewards and gets the hosts ones thru GiveHostReward (RPGManager.GiveReward)

public class NightCoopRewards extends ScriptableService {
  private let m_takeHostRewards: Bool;
  private let m_shareOurRewards: Bool;
  private let m_outgoing: array<TweakDBID>;
  private let m_skipped: Int32;
  private let m_given: Int32;
  private let m_sent: Int32;

  public static func Get() -> ref<NightCoopRewards> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopRewards") as NightCoopRewards;
  }

  public func Configure(takeHostRewards: Bool, shareOurRewards: Bool) {
    this.m_takeHostRewards = takeHostRewards;
    this.m_shareOurRewards = shareOurRewards;
  }

  public func SkipsOwn(reward: TweakDBID) -> Bool {
    if this.m_takeHostRewards {
      this.m_skipped += 1;
      return true;
    }
    if this.m_shareOurRewards && TDBID.IsValid(reward) {
      ArrayPush(this.m_outgoing, reward);
      this.m_sent += 1;
    }
    return false;
  }

  public func TakeOutgoing() -> array<TweakDBID> {
    let taken = this.m_outgoing;
    ArrayClear(this.m_outgoing);
    return taken;
  }

  public func GiveHostReward(reward: TweakDBID) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) || !TDBID.IsValid(reward) {
      return false;
    }
    RPGManager.GiveReward(player.GetGame(), reward, Cast<StatsObjectID>(player.GetEntityID()), 1.0);
    this.m_given += 1;
    return true;
  }

  public func Describe() -> String {
    return "sent " + IntToString(this.m_sent) + ", own skipped " + IntToString(this.m_skipped) + ", host rewards given " + IntToString(this.m_given);
  }
}

@wrapMethod(PlayerPuppet)
protected cb func OnRewardEvent(evt: ref<RewardEvent>) -> Bool {
  let rewards = NightCoopRewards.Get();
  if IsDefined(rewards) && IsDefined(evt) && rewards.SkipsOwn(evt.rewardName) {
    return false;
  }
  return wrappedMethod(evt);
}

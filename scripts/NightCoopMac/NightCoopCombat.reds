// npc combat, the script half of the plugins NpcCombat module (nightcoops damageclaims)
// a copy of a partners npc is immortal here (NightCoopCopy) so our hits never kill it by themselfs. evry hit our player lands
// on a copy becomes a claim (how much of its max health our game took, the attack record and type, the hit place and the
// flags) that the plugin takes evry tick and sends to the owner of that npc as HitClaim. before a copy dies with its owners
// npc the plugin lets go of the immortality and turns the kill reward off unless we killed it
// on the owner the plugin hands a partners claim to ApplyClaim: the health goes down as damage from that partners body and an
// npc left at 0.15 % or hit by a lethal claim gets killed by the body

public class NightCoopCombatClaim {
  public let target: EntityID;
  public let percent: Float;
  public let attackRecord: TweakDBID;
  public let attackType: Int32;
  public let charge: Float;
  public let hitPosition: Vector4;
  public let targetPosition: Vector4;
  public let hitDirection: Vector4;
  public let flags: array<Int32>;
}

public class NightCoopCombat extends ScriptableService {
  private let m_copies: array<EntityID>;
  private let m_claims: array<ref<NightCoopCombatClaim>>;
  private let m_hitsOnCopies: Int32;
  private let m_claimsMade: Int32;

  public static func Get() -> ref<NightCoopCombat> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopCombat") as NightCoopCombat;
  }

  public func MarkCopy(copy: ref<GameObject>) {
    if !IsDefined(copy) {
      return;
    }
    let id = copy.GetEntityID();
    if !ArrayContains(this.m_copies, id) {
      ArrayPush(this.m_copies, id);
    }
    GameInstance.GetGodModeSystem(copy.GetGame()).AddGodMode(id, gameGodModeType.Immortal, n"NightCoopCopy");
  }

  public func ForgetCopy(id: EntityID) {
    ArrayRemove(this.m_copies, id);
  }

  public func ForgetAllCopies() {
    ArrayClear(this.m_copies);
    ArrayClear(this.m_claims);
  }

  public func IsCopy(id: EntityID) -> Bool {
    return ArrayContains(this.m_copies, id);
  }

  public func ReleaseCopy(copy: ref<GameObject>, rewardUs: Bool) {
    if !IsDefined(copy) {
      return;
    }
    GameInstance.GetGodModeSystem(copy.GetGame()).RemoveGodMode(copy.GetEntityID(), gameGodModeType.Immortal, n"NightCoopCopy");
    if !rewardUs {
      copy.DisableKillReward(true);
    }
  }

  public func NoteHit(target: ref<ScriptedPuppet>, evt: ref<gameDamageReceivedEvent>) {
    let hit = evt.hitEvent;
    if !IsDefined(hit) || !IsDefined(hit.attackData) {
      return;
    }
    this.m_hitsOnCopies += 1;
    let game = target.GetGame();
    let player = GetPlayer(game);
    let instigator = hit.attackData.GetInstigator();
    if !IsDefined(player) || !IsDefined(instigator) || instigator.GetEntityID() != player.GetEntityID() {
      return;
    }
    let maxHealth = GameInstance.GetStatPoolsSystem(game).GetStatPoolMaxPointValue(Cast<StatsObjectID>(target.GetEntityID()), gamedataStatPoolType.Health);
    if maxHealth <= 0.0 || evt.totalDamageReceived <= 0.0 {
      return;
    }
    let claim = new NightCoopCombatClaim();
    claim.target = target.GetEntityID();
    claim.percent = MinF(100.0, evt.totalDamageReceived / maxHealth * 100.0);
    let definition = hit.attackData.GetAttackDefinition();
    if IsDefined(definition) {
      let record = definition.GetRecord();
      if IsDefined(record) {
        claim.attackRecord = record.GetID();
      }
    }
    claim.attackType = EnumInt(hit.attackData.GetAttackType());
    claim.charge = hit.attackData.GetWeaponCharge();
    claim.hitPosition = hit.hitPosition;
    claim.targetPosition = target.GetWorldPosition();
    claim.hitDirection = hit.hitDirection;
    for flag in hit.attackData.GetFlags() {
      ArrayPush(claim.flags, EnumInt(flag.flag));
    }
    ArrayPush(this.m_claims, claim);
    this.m_claimsMade += 1;
  }

  public func TakeClaims() -> array<ref<NightCoopCombatClaim>> {
    let taken = this.m_claims;
    ArrayClear(this.m_claims);
    return taken;
  }

  public func ApplyClaim(npc: ref<ScriptedPuppet>, body: ref<GameObject>, percent: Float, lethal: Bool) -> Float {
    if !IsDefined(npc) || npc.IsDead() {
      return 0.0;
    }
    let pools = GameInstance.GetStatPoolsSystem(npc.GetGame());
    let id = Cast<StatsObjectID>(npc.GetEntityID());
    let before = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, true);
    pools.RequestChangingStatPoolValue(id, gamedataStatPoolType.Health, -percent, body, false, true, false);
    let after = MaxF(0.0, before - percent);
    if lethal || after <= 0.15 {
      npc.Kill(body, false, false);
      after = 0.0;
    }
    return after;
  }

  public func TestClaimOnFirstCopy(percent: Float) {
    if ArraySize(this.m_copies) == 0 {
      return;
    }
    let claim = new NightCoopCombatClaim();
    claim.target = this.m_copies[0];
    claim.percent = percent;
    claim.attackType = 10;
    claim.hitPosition = new Vector4(0.0, 0.0, 0.0, 0.0);
    ArrayPush(claim.flags, 26);
    ArrayPush(claim.flags, 15);
    ArrayPush(this.m_claims, claim);
    this.m_claimsMade += 1;
  }

  public func Describe() -> String {
    return "copies " + IntToString(ArraySize(this.m_copies)) + ", hits on copies " + IntToString(this.m_hitsOnCopies) + ", claims made " +
      IntToString(this.m_claimsMade);
  }
}

@wrapMethod(ScriptedPuppet)
protected cb func OnDamageReceived(evt: ref<gameDamageReceivedEvent>) -> Bool {
  let combat = NightCoopCombat.Get();
  if IsDefined(combat) && combat.IsCopy(this.GetEntityID()) {
    combat.NoteHit(this, evt);
  }
  return wrappedMethod(evt);
}

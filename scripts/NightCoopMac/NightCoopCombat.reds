// npc combat, the script half of the plugins NpcCombat module (nightcoops damageclaims)
// a copy of a partners npc is immortal here (NightCoopCopy) so our hits never kill it by themselfs. evry hit our player lands
// on a copy becomes a claim (how much of its max health our game took, the attack record and type, the hit place and the
// flags) that the plugin takes evry tick and sends to the owner of that npc as HitClaim. before a copy dies with its owners
// npc the plugin lets go of the immortality and turns the kill reward off unless we killed it
// on the owner the plugin hands a partners claim to ApplyClaim: the health goes down as damage from that partners body and an
// npc left at 0.15 % or hit by a lethal claim gets killed by the body
// partner bodies are marked too: an npc hit landing on one is kept as a body hit the plugin sends to that partner as
// PlayerHit. a PlayerHit from a partner gets rebuilt here like nightcoop does (the attack from its record with our copy of the
// attacker as instigator, our place plus the hit offset, the flags) and queued on our player so our own game deals the damage

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

public class NightCoopBodyHit {
  public let peer: Int32;
  public let attacker: EntityID;
  public let attackRecord: TweakDBID;
  public let attackType: Int32;
  public let charge: Float;
  public let hitPosition: Vector4;
  public let targetPosition: Vector4;
  public let hitDirection: Vector4;
  public let flags: array<Int32>;
}

public class NightCoopCombat extends ScriptableService {
  private let m_bodies: array<EntityID>;
  private let m_bodyPeers: array<Int32>;
  private let m_bodyHits: array<ref<NightCoopBodyHit>>;
  private let m_hitsApplied: Int32;
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

  public func MarkBody(body: ref<GameObject>, peer: Int32) {
    if !IsDefined(body) {
      return;
    }
    let id = body.GetEntityID();
    let index = ArrayFindFirst(this.m_bodies, id);
    if index >= 0 {
      this.m_bodyPeers[index] = peer;
      return;
    }
    ArrayPush(this.m_bodies, id);
    ArrayPush(this.m_bodyPeers, peer);
  }

  public func ForgetBodies() {
    ArrayClear(this.m_bodies);
    ArrayClear(this.m_bodyPeers);
    ArrayClear(this.m_bodyHits);
  }

  public func BodyPeer(id: EntityID) -> Int32 {
    let index = ArrayFindFirst(this.m_bodies, id);
    return index >= 0 ? this.m_bodyPeers[index] : -1;
  }

  public func NoteBodyHit(body: ref<GameObject>, peer: Int32, hit: ref<gameHitEvent>) {
    if !IsDefined(hit) || !IsDefined(hit.attackData) {
      return;
    }
    let attacker = hit.attackData.GetInstigator();
    if !IsDefined(attacker) || attacker.IsPlayer() {
      return;
    }
    let bodyHit = new NightCoopBodyHit();
    bodyHit.peer = peer;
    bodyHit.attacker = attacker.GetEntityID();
    let definition = hit.attackData.GetAttackDefinition();
    if IsDefined(definition) {
      let record = definition.GetRecord();
      if IsDefined(record) {
        bodyHit.attackRecord = record.GetID();
      }
    }
    bodyHit.attackType = EnumInt(hit.attackData.GetAttackType());
    bodyHit.charge = hit.attackData.GetWeaponCharge();
    bodyHit.hitPosition = hit.hitPosition;
    bodyHit.targetPosition = body.GetWorldPosition();
    bodyHit.hitDirection = hit.hitDirection;
    for flag in hit.attackData.GetFlags() {
      ArrayPush(bodyHit.flags, EnumInt(flag.flag));
    }
    ArrayPush(this.m_bodyHits, bodyHit);
  }

  public func TakeBodyHits() -> array<ref<NightCoopBodyHit>> {
    let taken = this.m_bodyHits;
    ArrayClear(this.m_bodyHits);
    return taken;
  }

  public func ApplyPlayerHit(attacker: ref<GameObject>, attackRecord: TweakDBID, attackType: Int32, charge: Float, offset: Vector4, direction: Vector4,
                             flags: array<Int32>) -> Bool {
    if !IsDefined(attacker) {
      return false;
    }
    let game = attacker.GetGame();
    let player = GetPlayer(game);
    let record = TweakDBInterface.GetAttackRecord(attackRecord);
    if !IsDefined(player) || !IsDefined(record) {
      return false;
    }
    let weapon = GameInstance.GetTransactionSystem(game).GetItemInSlot(attacker, t"AttachmentSlots.WeaponRight") as WeaponObject;
    let context: AttackInitContext;
    context.record = record;
    context.instigator = attacker;
    context.source = attacker;
    let attack = IAttack.Create(context);
    if !IsDefined(attack) {
      return false;
    }
    let attackData = new AttackData();
    attackData.SetAttackDefinition(attack);
    attackData.SetInstigator(attacker);
    attackData.SetSource(attacker);
    if IsDefined(weapon) {
      attackData.SetWeapon(weapon);
    }
    attackData.PreAttack();
    if attackType >= 0 && attackType < 15 {
      attackData.SetAttackType(IntEnum<gamedataAttackType>(attackType));
    }
    attackData.SetWeaponCharge(charge);
    attackData.SetAttackPosition(attacker.GetWorldPosition());
    for flag in flags {
      attackData.AddFlag(IntEnum<hitFlag>(flag), n"NightCoop");
    }
    let hit = new gameHitEvent();
    hit.attackData = attackData;
    hit.target = player;
    let place = player.GetWorldPosition();
    hit.hitPosition = new Vector4(place.X + offset.X, place.Y + offset.Y, place.Z + offset.Z, 1.0);
    hit.hitDirection = direction;
    GameInstance.GetDamageSystem(game).QueueHitEvent(hit, player);
    this.m_hitsApplied += 1;
    return true;
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
      IntToString(this.m_claimsMade) + ", bodies " + IntToString(ArraySize(this.m_bodies)) + ", partner hits on us " + IntToString(this.m_hitsApplied);
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

@wrapMethod(ScriptedPuppet)
protected cb func OnHit(evt: ref<gameHitEvent>) -> Bool {
  let combat = NightCoopCombat.Get();
  if IsDefined(combat) {
    let peer = combat.BodyPeer(this.GetEntityID());
    if peer >= 0 {
      combat.NoteBodyHit(this, peer, evt);
    }
  }
  return wrappedMethod(evt);
}

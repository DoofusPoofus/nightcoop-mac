// devices, the script half of the plugins DeviceSync (nightcoops devicesync, turret_host_led and barrels)
// while a world is shared the plugin turns shared on. then device actions our player finished (ScriptableDeviceAction.CompleteAction
// with our player as executor) land in doneActions with their ps id, ps class and the state before, devices our player
// destroyed (Device / SecurityTurret OnDeath) in deadIds, turrets that want our player as their target in turretWants and
// explosive devices that start to blow here in blastDevices. the plugin reads and clears all that evry 100 ms
// the apply side finds a partners device by its ps id + ps class (GetConstAccessToPSObject): Prepare + Run do an action the
// plugin built thru rtti, StepTo forces a state with the quest actions, MarkSkill passes a skill check, MarkQuickHacked
// queues a SetQuickHackEvent and Kill destroys the device (or disables it while its not streamed in). Kill gives 0 destroyed
// now, 1 broken already, 2 not in our stream (disabled for now), 3 no such device here
// a turret in calmTurrets does not take our player as its target (the hosts same turret is calm). an explosive device in
// quietBlasts blows without its damage and extra effect, the blast came from a partner and did its damage over there.
// Blast mode 0 blows quietly, 1 blows with damage (both thru Device.Die like nightcoop), 2 sets it exploded and hidden (a
// snapshot). it gives 0 done, 1 exploded already, 2 not in our stream, 3 another device at that id (more than 3 m off).
// FallbackBlow starts the explosion pipeline itself when Die did not start it

public class NightCoopDeviceSync extends ScriptableService {
  private let m_shared: Bool;
  private let m_doneActions: array<ref<ScriptableDeviceAction>>;
  private let m_doneIds: array<PersistentID>;
  private let m_doneClasses: array<CName>;
  private let m_doneStates: array<Int32>;
  private let m_deadIds: array<PersistentID>;
  private let m_deadClasses: array<CName>;
  private let m_turretWants: array<EntityID>;
  private let m_calmTurrets: array<EntityID>;
  private let m_blastDevices: array<EntityID>;
  private let m_blastPositions: array<Vector4>;
  private let m_blastOurs: array<Bool>;
  private let m_quietBlasts: array<EntityID>;
  private let m_expectedBlasts: array<EntityID>;

  public static func Get() -> ref<NightCoopDeviceSync> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopDeviceSync") as NightCoopDeviceSync;
  }

  public func Configure(shared: Bool) {
    this.m_shared = shared;
    if !shared {
      ArrayClear(this.m_calmTurrets);
      ArrayClear(this.m_quietBlasts);
      ArrayClear(this.m_expectedBlasts);
    }
  }

  public func ClearOutgoing() {
    ArrayClear(this.m_doneActions);
    ArrayClear(this.m_doneIds);
    ArrayClear(this.m_doneClasses);
    ArrayClear(this.m_doneStates);
    ArrayClear(this.m_deadIds);
    ArrayClear(this.m_deadClasses);
    ArrayClear(this.m_turretWants);
    ArrayClear(this.m_blastDevices);
    ArrayClear(this.m_blastPositions);
    ArrayClear(this.m_blastOurs);
  }

  public func IsOurPlayer(object: wref<GameObject>) -> Bool {
    if !IsDefined(object) {
      return false;
    }
    let player = GetPlayer(GetGameInstance());
    return IsDefined(player) && Equals(object.GetEntityID(), player.GetEntityID());
  }

  public func FindPS(id: PersistentID, className: CName) -> ref<ScriptableDeviceComponentPS> {
    return GameInstance.GetPersistencySystem(GetGameInstance()).GetConstAccessToPSObject(id, className) as ScriptableDeviceComponentPS;
  }

  public func StateOf(id: PersistentID, className: CName) -> Int32 {
    let ps = this.FindPS(id, className);
    if !IsDefined(ps) {
      return 127;
    }
    return ps.NightCoopState();
  }

  public func NoteDone(action: ref<ScriptableDeviceAction>) {
    if !this.m_shared || !IsDefined(action) || !this.IsOurPlayer(action.GetExecutor()) {
      return;
    }
    let id = action.GetPersistentID();
    let className = action.GetDeviceClassName();
    ArrayPush(this.m_doneActions, action);
    ArrayPush(this.m_doneIds, id);
    ArrayPush(this.m_doneClasses, className);
    ArrayPush(this.m_doneStates, this.StateOf(id, className));
  }

  public func NoteDeath(device: ref<Device>, instigator: wref<GameObject>) {
    if !this.m_shared || !IsDefined(device) || IsDefined(device as ExplosiveDevice) || !this.IsOurPlayer(instigator) {
      return;
    }
    let ps = device.GetDevicePS();
    if !IsDefined(ps) {
      return;
    }
    ArrayPush(this.m_deadIds, ps.GetID());
    ArrayPush(this.m_deadClasses, ps.GetClassName());
  }

  public func LetsGoOf(turret: ref<SecurityTurret>, target: wref<GameObject>) -> Bool {
    if !this.m_shared || !this.IsOurPlayer(target) {
      return false;
    }
    let id = turret.GetEntityID();
    if !ArrayContains(this.m_turretWants, id) {
      ArrayPush(this.m_turretWants, id);
    }
    return ArrayContains(this.m_calmTurrets, id);
  }

  public func SetTurretCalm(turret: EntityID, calm: Bool) {
    let known = ArrayContains(this.m_calmTurrets, turret);
    if calm && !known {
      ArrayPush(this.m_calmTurrets, turret);
    } else if !calm && known {
      ArrayRemove(this.m_calmTurrets, turret);
    }
  }

  public func TurretState(turret: EntityID) -> Int32 {
    let device = GameInstance.FindEntityByID(GetGameInstance(), turret) as SensorDevice;
    if !IsDefined(device) {
      return 0;
    }
    return IsDefined(device.GetCurrentlyFollowedTarget()) ? 2 : 1;
  }

  public func ReevaluateTurret(turret: EntityID) -> Bool {
    let device = GameInstance.FindEntityByID(GetGameInstance(), turret) as SensorDevice;
    if !IsDefined(device) {
      return false;
    }
    device.NightCoopReevaluateTargets();
    return true;
  }

  public func NoteBlast(device: ref<ExplosiveDevice>, instigator: wref<GameObject>) {
    if !IsDefined(device) {
      return;
    }
    let id = device.GetEntityID();
    if ArrayContains(this.m_expectedBlasts, id) {
      ArrayRemove(this.m_expectedBlasts, id);
      return;
    }
    if !this.m_shared || this.IsQuiet(device) {
      return;
    }
    if device.GetDevicePS().IsExploded() {
      return;
    }
    ArrayPush(this.m_blastDevices, device.GetEntityID());
    ArrayPush(this.m_blastPositions, device.GetWorldPosition());
    ArrayPush(this.m_blastOurs, this.IsOurPlayer(instigator));
  }

  public func IsQuiet(device: ref<ExplosiveDevice>) -> Bool {
    return ArrayContains(this.m_quietBlasts, device.GetEntityID());
  }

  public func EndQuiet(device: EntityID) {
    ArrayRemove(this.m_quietBlasts, device);
  }

  public func Prepare(action: ref<ScriptableDeviceAction>, id: PersistentID, className: CName) -> Bool {
    let ps = this.FindPS(id, className);
    if !IsDefined(ps) || !IsDefined(action) {
      return false;
    }
    action.SetUp(ps);
    return true;
  }

  public func Run(action: ref<ScriptableDeviceAction>, id: PersistentID, className: CName) -> Bool {
    let ps = this.FindPS(id, className);
    if !IsDefined(ps) || !IsDefined(action) {
      return false;
    }
    action.SetExecutor(GetPlayer(GetGameInstance()));
    ps.ExecutePSAction(action, ps);
    return true;
  }

  public func StepTo(id: PersistentID, className: CName, state: Int32) -> Bool {
    let ps = this.FindPS(id, className);
    return IsDefined(ps) && ps.NightCoopStepTo(state);
  }

  public func MarkSkill(id: PersistentID, className: CName, skill: Int32) -> Bool {
    let ps = this.FindPS(id, className);
    return IsDefined(ps) && ps.NightCoopMarkSkill(skill);
  }

  public func MarkQuickHacked(id: PersistentID, className: CName, name: CName) -> Bool {
    let ps = this.FindPS(id, className);
    if !IsDefined(ps) {
      return false;
    }
    let evt = new SetQuickHackEvent();
    evt.wasQuickHacked = true;
    evt.quickHackName = name;
    GameInstance.GetPersistencySystem(GetGameInstance()).QueuePSEvent(id, className, evt);
    return true;
  }

  public func Kill(id: PersistentID, className: CName) -> Int32 {
    let ps = this.FindPS(id, className);
    if !IsDefined(ps) {
      return 3;
    }
    if ps.IsBroken() {
      return 1;
    }
    let device = GameInstance.FindEntityByID(GetGameInstance(), PersistentID.ExtractEntityID(id)) as Device;
    if !IsDefined(device) {
      ps.NightCoopStepTo(-2);
      return 2;
    }
    device.NightCoopDie();
    return 0;
  }

  public func Blast(device: EntityID, x: Float, y: Float, z: Float, mode: Int32) -> Int32 {
    let explosive = GameInstance.FindEntityByID(GetGameInstance(), device) as ExplosiveDevice;
    if !IsDefined(explosive) {
      return 2;
    }
    if Vector4.Distance(explosive.GetWorldPosition(), new Vector4(x, y, z, 1.0)) > 3.0 {
      return 3;
    }
    if explosive.GetDevicePS().IsExploded() {
      return 1;
    }
    if mode == 2 {
      explosive.NightCoopSetExplodedQuietly();
      return 0;
    }
    if mode == 0 && !ArrayContains(this.m_quietBlasts, device) {
      ArrayPush(this.m_quietBlasts, device);
    }
    if !ArrayContains(this.m_expectedBlasts, device) {
      ArrayPush(this.m_expectedBlasts, device);
    }
    explosive.NightCoopDie();
    return 0;
  }

  public func FallbackBlow(device: EntityID) -> Bool {
    if !ArrayContains(this.m_expectedBlasts, device) {
      return false;
    }
    let explosive = GameInstance.FindEntityByID(GetGameInstance(), device) as ExplosiveDevice;
    if !IsDefined(explosive) || explosive.GetDevicePS().IsExploded() {
      ArrayRemove(this.m_expectedBlasts, device);
      return false;
    }
    explosive.NightCoopBlow();
    ArrayRemove(this.m_expectedBlasts, device);
    return true;
  }
}

@addMethod(ScriptableDeviceComponentPS)
public func NightCoopState() -> Int32 {
  let state = this.GetDeviceState();
  if Equals(state, EDeviceStatus.DISABLED) {
    return -2;
  }
  if Equals(state, EDeviceStatus.UNPOWERED) {
    return -1;
  }
  if Equals(state, EDeviceStatus.OFF) {
    return 0;
  }
  if Equals(state, EDeviceStatus.ON) {
    return 1;
  }
  return 2;
}

@addMethod(ScriptableDeviceComponentPS)
public func NightCoopStepTo(state: Int32) -> Bool {
  if state == 1 {
    this.ExecutePSAction(this.ActionQuestForceON(), this);
    return true;
  }
  if state == 0 {
    this.ExecutePSAction(this.ActionQuestForceOFF(), this);
    return true;
  }
  if state == -1 {
    this.ExecutePSAction(this.ActionQuestForceUnpower(), this);
    return true;
  }
  if state == -2 {
    this.ExecutePSAction(this.ActionQuestForceDisabled(), this);
    return true;
  }
  return false;
}

@addMethod(ScriptableDeviceComponentPS)
public func NightCoopMarkSkill(skill: Int32) -> Bool {
  let container = this.GetSkillCheckContainer();
  if !IsDefined(container) {
    return false;
  }
  let check: ref<SkillCheckBase>;
  if skill == 1 {
    check = container.GetHackingSlot();
  } else if skill == 2 {
    check = container.GetEngineeringSlot();
  } else if skill == 3 {
    check = container.GetDemolitionSlot();
  }
  if !IsDefined(check) {
    return false;
  }
  check.SetIsActive(true);
  check.SetIsPassed(true);
  check.CheckPerformed();
  return true;
}

@addMethod(Device)
public func NightCoopDie() {
  this.Die();
}

@addMethod(SensorDevice)
public func NightCoopReevaluateTargets() {
  this.ReevaluateTargets();
}

@addMethod(ExplosiveDevice)
public func NightCoopBlow() {
  this.StartExplosionPipeline(null);
}

@addMethod(ExplosiveDevice)
public func NightCoopSetExplodedQuietly() {
  this.GetDevicePS().SetExplodedState(true);
  this.ToggleVisibility(false);
}

@wrapMethod(ScriptableDeviceAction)
public func CompleteAction(gameInstance: GameInstance) {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) {
    sync.NoteDone(this);
  }
  wrappedMethod(gameInstance);
}

@wrapMethod(Device)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) {
    sync.NoteDeath(this, evt.instigator);
  }
  return wrappedMethod(evt);
}

@wrapMethod(SecurityTurret)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) {
    sync.NoteDeath(this, evt.instigator);
  }
  return wrappedMethod(evt);
}

@wrapMethod(SecurityTurret)
protected func SetAsIntrestingTarget(target: wref<GameObject>) -> Bool {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) && sync.LetsGoOf(this, target) {
    return false;
  }
  return wrappedMethod(target);
}

@wrapMethod(ExplosiveDevice)
protected func StartExplosionPipeline(instigator: wref<GameObject>, opt additionalDelay: Float) {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) {
    sync.NoteBlast(this, instigator);
  }
  wrappedMethod(instigator, additionalDelay);
}

@wrapMethod(ExplosiveDevice)
protected func DoAttack(damageType: TweakDBID, opt instigator: wref<GameObject>) {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) && sync.IsQuiet(this) {
    return;
  }
  wrappedMethod(damageType, instigator);
}

@wrapMethod(ExplosiveDevice)
protected func DoAdditionalGameEffect(additionalGameEffectType: EExplosiveAdditionalGameEffectType) {
  let sync = NightCoopDeviceSync.Get();
  if IsDefined(sync) && sync.IsQuiet(this) {
    return;
  }
  wrappedMethod(additionalGameEffectType);
}

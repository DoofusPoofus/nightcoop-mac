// doors the script half of nightcoops doorsync (DoorSync 88 / the plugin half is src/plugin/DoorSync.cpp). while a world is
// shared it queues the doors the plugin should look at: a door that moved here (MoveDoor) a quest action on a door
// (GetQuestActionByName) and an auto close we let thru / each with the state the door had just before. the plugin reads
// ReadDoor of the doors it tracks evry 500 - 1000 ms and moves ours with ApplyDoor (quest force unseal unlock lock seal and
// OpenDoor / CloseDoor). the auto close guard swallows OnDoorTriggerDelayedEvent while a partner (spots from the plugin evry
// 100 ms) stands within 4 m flat and 3 m up and queues the same event again once they left. lift doors stay with the lifts

public class NightCoopDoorSync extends ScriptableService {
  private let m_shared: Bool;
  private let m_seenDoors: array<EntityID>;
  private let m_seenBefore: array<Int32>;
  private let m_seenCause: array<Int32>;
  private let m_seenQuestOp: array<Int32>;
  private let m_partnerPeers: array<Int32>;
  private let m_partnerSpots: array<Vector4>;
  private let m_heldDoors: array<EntityID>;
  private let m_heldEvents: array<ref<DoorTriggerDelayedEvent>>;

  public static func Get() -> ref<NightCoopDoorSync> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopDoorSync") as NightCoopDoorSync;
  }

  public static func StateBits(state: ref<DoorControllerPS>) -> Int32 {
    let bits = 0;
    if state.IsOpen() {
      bits += 1;
    }
    if state.IsLocked() {
      bits += 2;
    }
    if state.IsSealed() {
      bits += 4;
    }
    return bits;
  }

  public static func QuestOpOf(actionName: CName) -> Int32 {
    if Equals(actionName, n"ForceOpen") {
      return 1;
    }
    if Equals(actionName, n"ForceClose") {
      return 2;
    }
    if Equals(actionName, n"ForceCloseImmediate") {
      return 3;
    }
    if Equals(actionName, n"ForceOpenScene") {
      return 4;
    }
    if Equals(actionName, n"ForceCloseScene") {
      return 5;
    }
    if Equals(actionName, n"ForceLock") {
      return 6;
    }
    if Equals(actionName, n"ForceUnlock") {
      return 7;
    }
    if Equals(actionName, n"ForceSeal") {
      return 8;
    }
    if Equals(actionName, n"ForceUnseal") {
      return 9;
    }
    return 10;
  }

  public func Configure(shared: Bool) {
    if this.m_shared && !shared {
      this.ClearOutgoing();
      this.ClearPartners();
      this.LetHeldDoorsGo();
    }
    this.m_shared = shared;
  }

  public func ClearOutgoing() {
    ArrayClear(this.m_seenDoors);
    ArrayClear(this.m_seenBefore);
    ArrayClear(this.m_seenCause);
    ArrayClear(this.m_seenQuestOp);
  }

  public func ClearPartners() {
    ArrayClear(this.m_partnerPeers);
    ArrayClear(this.m_partnerSpots);
  }

  public func AddPartner(peer: Int32, x: Float, y: Float, z: Float) {
    ArrayPush(this.m_partnerPeers, peer);
    ArrayPush(this.m_partnerSpots, new Vector4(x, y, z, 1.0));
  }

  public func ReadDoor(door: EntityID) -> Int32 {
    let device = GameInstance.FindEntityByID(GetGameInstance(), door) as Door;
    if !IsDefined(device) {
      return -1;
    }
    let state = device.GetDevicePS();
    if !IsDefined(state) {
      return -1;
    }
    if state.IsLiftDoor() {
      return -2;
    }
    return NightCoopDoorSync.StateBits(state);
  }

  public func ApplyDoor(door: EntityID, wanted: Int32, evenWhenBusy: Bool) -> Int32 {
    let device = GameInstance.FindEntityByID(GetGameInstance(), door) as Door;
    if !IsDefined(device) || !IsDefined(device.GetDevicePS()) {
      return -1;
    }
    return device.NightCoopMoveTo(wanted, evenWhenBusy);
  }

  public func OurSide(door: EntityID) -> Int32 {
    let device = GameInstance.FindEntityByID(GetGameInstance(), door) as Door;
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(device) || !IsDefined(player) {
      return 0;
    }
    let side = device.GetDoorTriggerSide(player);
    if Equals(side, EDoorTriggerSide.ONE) {
      return 1;
    }
    if Equals(side, EDoorTriggerSide.TWO) {
      return 2;
    }
    return 0;
  }

  public func AnybodyAt(door: EntityID, exceptPeer: Int32) -> Bool {
    let device = GameInstance.FindEntityByID(GetGameInstance(), door) as Door;
    if !IsDefined(device) {
      return false;
    }
    let spot = device.GetWorldPosition();
    let player = GetPlayer(GetGameInstance());
    if IsDefined(player) && NightCoopDoorSync.Near(spot, player.GetWorldPosition()) {
      return true;
    }
    return this.PartnerNear(spot, exceptPeer);
  }

  public func ServeHeld() {
    let index = ArraySize(this.m_heldDoors) - 1;
    while index >= 0 {
      let device = GameInstance.FindEntityByID(GetGameInstance(), this.m_heldDoors[index]) as Door;
      if !IsDefined(device) || !IsDefined(device.GetDevicePS()) || !device.GetDevicePS().IsOpen() {
        ArrayErase(this.m_heldDoors, index);
        ArrayErase(this.m_heldEvents, index);
      } else {
        if !this.m_shared || !this.PartnerNear(device.GetWorldPosition(), -1) {
          let held = this.m_heldEvents[index];
          ArrayErase(this.m_heldDoors, index);
          ArrayErase(this.m_heldEvents, index);
          device.QueueEvent(held);
        }
      }
      index -= 1;
    }
  }

  public func HeldCount() -> Int32 {
    return ArraySize(this.m_heldDoors);
  }

  public func HoldsAutoClose(door: ref<Door>, evt: ref<DoorTriggerDelayedEvent>) -> Bool {
    if !this.m_shared {
      return false;
    }
    let state = door.GetDevicePS();
    if !IsDefined(state) || state.IsLiftDoor() {
      return false;
    }
    let id = door.GetEntityID();
    if this.PartnerNear(door.GetWorldPosition(), -1) {
      let index = 0;
      while index < ArraySize(this.m_heldDoors) {
        if Equals(this.m_heldDoors[index], id) {
          this.m_heldEvents[index] = evt;
          return true;
        }
        index += 1;
      }
      ArrayPush(this.m_heldDoors, id);
      ArrayPush(this.m_heldEvents, evt);
      return true;
    }
    this.Note(id, NightCoopDoorSync.StateBits(state), 1, 0);
    return false;
  }

  public func NoteMove(door: ref<Door>, wasOpen: Bool, shouldBeOpened: Bool) {
    if !this.m_shared {
      return;
    }
    let state = door.GetDevicePS();
    if !IsDefined(state) || state.IsLiftDoor() {
      return;
    }
    let before = -1;
    if NotEquals(wasOpen, shouldBeOpened) {
      before = NightCoopDoorSync.StateBits(state);
      if state.IsOpen() {
        before -= 1;
      }
      if wasOpen {
        before += 1;
      }
    }
    this.Note(door.GetEntityID(), before, 0, 0);
  }

  public func NoteQuestAction(state: ref<DoorControllerPS>, actionName: CName) {
    if !this.m_shared || state.IsLiftDoor() {
      return;
    }
    this.Note(state.GetMyEntityID(), NightCoopDoorSync.StateBits(state), 2, NightCoopDoorSync.QuestOpOf(actionName));
  }

  private func Note(door: EntityID, before: Int32, cause: Int32, questOp: Int32) {
    if ArraySize(this.m_seenDoors) >= 256 {
      return;
    }
    ArrayPush(this.m_seenDoors, door);
    ArrayPush(this.m_seenBefore, before);
    ArrayPush(this.m_seenCause, cause);
    ArrayPush(this.m_seenQuestOp, questOp);
  }

  private func LetHeldDoorsGo() {
    let index = ArraySize(this.m_heldDoors) - 1;
    while index >= 0 {
      let device = GameInstance.FindEntityByID(GetGameInstance(), this.m_heldDoors[index]) as Door;
      if IsDefined(device) {
        device.QueueEvent(this.m_heldEvents[index]);
      }
      index -= 1;
    }
    ArrayClear(this.m_heldDoors);
    ArrayClear(this.m_heldEvents);
  }

  private func PartnerNear(spot: Vector4, exceptPeer: Int32) -> Bool {
    let index = 0;
    while index < ArraySize(this.m_partnerSpots) {
      if this.m_partnerPeers[index] != exceptPeer && NightCoopDoorSync.Near(spot, this.m_partnerSpots[index]) {
        return true;
      }
      index += 1;
    }
    return false;
  }

  private static func Near(door: Vector4, someone: Vector4) -> Bool {
    let acrossX = someone.X - door.X;
    let acrossY = someone.Y - door.Y;
    return acrossX * acrossX + acrossY * acrossY <= 16.0 && AbsF(someone.Z - door.Z) <= 3.0;
  }
}

@addMethod(Door)
public func NightCoopMoveTo(wanted: Int32, evenWhenBusy: Bool) -> Int32 {
  let state = this.GetDevicePS();
  if state.IsLiftDoor() {
    return -2;
  }
  if state.IsBusy() && !evenWhenBusy {
    return -3;
  }
  let wantOpen = (wanted & 1) != 0;
  let wantLocked = (wanted & 2) != 0;
  let wantSealed = (wanted & 4) != 0;
  let isOpen = state.IsOpen();
  let isLocked = state.IsLocked();
  let isSealed = state.IsSealed();
  if Equals(isOpen, wantOpen) && Equals(isLocked, wantLocked) && Equals(isSealed, wantSealed) {
    return 1;
  }
  let opening = wantOpen && !isOpen;
  if isSealed && (!wantSealed || opening) {
    state.NightCoopUnseal();
  }
  if isLocked && (!wantLocked || opening) {
    state.NightCoopUnlock();
  }
  if opening {
    this.OpenDoor();
  }
  if !wantOpen && isOpen {
    this.CloseDoor();
  }
  if wantLocked && (!isLocked || opening) {
    state.NightCoopLock();
  }
  if wantSealed && (!isSealed || opening) {
    state.NightCoopSeal();
  }
  return 0;
}

@addMethod(DoorControllerPS)
public func NightCoopUnseal() {
  this.ExecutePSAction(this.ActionQuestForceUnseal(), this);
}

@addMethod(DoorControllerPS)
public func NightCoopUnlock() {
  this.ExecutePSAction(this.ActionQuestForceUnlock(), this);
}

@addMethod(DoorControllerPS)
public func NightCoopLock() {
  this.ExecutePSAction(this.ActionQuestForceLock(), this);
}

@addMethod(DoorControllerPS)
public func NightCoopSeal() {
  this.ExecutePSAction(this.ActionQuestForceSeal(), this);
}

@wrapMethod(Door)
protected func MoveDoor(shouldBeOpened: Bool, secondFlag: Bool, movingSpeedMultiplier: Float) {
  let wasOpen = this.m_isVisuallyOpened;
  wrappedMethod(shouldBeOpened, secondFlag, movingSpeedMultiplier);
  let sync = NightCoopDoorSync.Get();
  if IsDefined(sync) {
    sync.NoteMove(this, wasOpen, shouldBeOpened);
  }
}

@wrapMethod(Door)
protected cb func OnDoorTriggerDelayedEvent(evt: ref<DoorTriggerDelayedEvent>) -> Bool {
  let sync = NightCoopDoorSync.Get();
  if IsDefined(sync) && sync.HoldsAutoClose(this, evt) {
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(DoorControllerPS)
public func GetQuestActionByName(actionName: CName) -> ref<DeviceAction> {
  let sync = NightCoopDoorSync.Get();
  if IsDefined(sync) {
    sync.NoteQuestAction(this, actionName);
  }
  return wrappedMethod(actionName);
}

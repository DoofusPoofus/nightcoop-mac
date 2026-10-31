// lifts, the area guard of nightcoops liftsync: a lift reacts to whoever walks into its cab or call area, and our partner
// bodies and mirror cars would call or hold lifts nobody here asked for. their enter / exit events are swallowed, evryone
// elses go thru as before. the shared one cab trips of nightcoop (LiftSync 86 / LiftRide 97) are not done yet

public abstract class NightCoopLifts {
  public static func IsOurs(activator: EntityGameInterface) -> Bool {
    let entity = EntityGameInterface.GetEntity(activator);
    if !IsDefined(entity) {
      return false;
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    let id = entity.GetEntityID();
    return entities.IsTagged(id, n"NightCoopPartnerBody") || entities.IsTagged(id, n"NightCoopVehicleMirror");
  }
}

@wrapMethod(LiftDevice)
protected cb func OnAreaEnter(evt: ref<AreaEnteredEvent>) -> Bool {
  if NightCoopLifts.IsOurs(evt.activator) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(LiftDevice)
protected cb func OnAreaExit(evt: ref<AreaExitedEvent>) -> Bool {
  if NightCoopLifts.IsOurs(evt.activator) {
    return false;
  }
  return wrappedMethod(evt);
}

// one cab lifts (nightcoops lift_one_cab): in co-op the host owns evry lift. on a guest a button or call press does not start
// the guests own cab, it lands in m_requestLifts / m_requestFloors for the plugin to ask the host (LiftSync kind 3). on the
// host evry start lands in m_departureLifts / m_departureFloors for the plugin to tell evryone (kind 2), and StartLift starts a
// cab for a guests request or for a hosts departure (the bypass keeps that start from being caught again on a guest).
// in step start: Hold makes the next LiftStartDelayEvent of a lift wait until the plugin calls Release at the departure time,
// so evry copy of the cab leaves together. UseSpeed makes a guests copy run at the hosts speed while it starts (nightcoop
// redirects GetLiftSpeed inside OnLiftStartDelayEvent the same way, an empty cab would run slower and drift apart)
public class NightCoopLiftSync extends ScriptableService {
  private let m_oneCab: Bool;
  private let m_isHost: Bool;
  private let m_bypass: Bool;
  private let m_requestLifts: array<EntityID>;
  private let m_requestFloors: array<Int32>;
  private let m_departureLifts: array<EntityID>;
  private let m_departureFloors: array<Int32>;
  private let m_departureDelays: array<Float>;
  private let m_departureSpeeds: array<Float>;
  private let m_holdLifts: array<EntityID>;
  private let m_heldLifts: array<EntityID>;
  private let m_heldEvents: array<ref<LiftStartDelayEvent>>;
  private let m_speedLifts: array<EntityID>;
  private let m_speedValues: array<Float>;
  private let m_startingLift: EntityID;
  private let m_starting: Bool;

  public static func Get() -> ref<NightCoopLiftSync> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopLiftSync") as NightCoopLiftSync;
  }

  public func Configure(oneCab: Bool, isHost: Bool) {
    this.m_oneCab = oneCab;
    this.m_isHost = isHost;
  }

  public func ClearOutgoing() {
    ArrayClear(this.m_requestLifts);
    ArrayClear(this.m_requestFloors);
    ArrayClear(this.m_departureLifts);
    ArrayClear(this.m_departureFloors);
    ArrayClear(this.m_departureDelays);
    ArrayClear(this.m_departureSpeeds);
  }

  public func Hold(lift: EntityID) {
    ArrayPush(this.m_holdLifts, lift);
  }

  public func Release(lift: EntityID) -> Bool {
    let index = 0;
    while index < ArraySize(this.m_heldLifts) {
      if Equals(this.m_heldLifts[index], lift) {
        let device = GameInstance.FindEntityByID(GetGameInstance(), lift) as LiftDevice;
        let held = this.m_heldEvents[index];
        ArrayErase(this.m_heldLifts, index);
        ArrayErase(this.m_heldEvents, index);
        if IsDefined(device) && IsDefined(held) {
          device.QueueEvent(held);
          return true;
        }
        return false;
      }
      index += 1;
    }
    let pending = 0;
    while pending < ArraySize(this.m_holdLifts) {
      if Equals(this.m_holdLifts[pending], lift) {
        ArrayErase(this.m_holdLifts, pending);
        return false;
      }
      pending += 1;
    }
    return false;
  }

  public func UseSpeed(lift: EntityID, speed: Float) {
    ArrayPush(this.m_speedLifts, lift);
    ArrayPush(this.m_speedValues, speed);
  }

  public func HoldsStart(device: ref<LiftDevice>, evt: ref<LiftStartDelayEvent>) -> Bool {
    let id = device.GetEntityID();
    let index = 0;
    while index < ArraySize(this.m_holdLifts) {
      if Equals(this.m_holdLifts[index], id) {
        ArrayErase(this.m_holdLifts, index);
        ArrayPush(this.m_heldLifts, id);
        ArrayPush(this.m_heldEvents, evt);
        return true;
      }
      index += 1;
    }
    return false;
  }

  public func BeginStart(device: ref<LiftDevice>) {
    this.m_startingLift = device.GetEntityID();
    this.m_starting = true;
  }

  public func EndStart() {
    this.m_starting = false;
    let index = 0;
    while index < ArraySize(this.m_speedLifts) {
      if Equals(this.m_speedLifts[index], this.m_startingLift) {
        ArrayErase(this.m_speedLifts, index);
        ArrayErase(this.m_speedValues, index);
        return;
      }
      index += 1;
    }
  }

  public func SpeedFor(lift: EntityID) -> Float {
    if !this.m_starting || !Equals(lift, this.m_startingLift) {
      return -1.0;
    }
    let index = 0;
    while index < ArraySize(this.m_speedLifts) {
      if Equals(this.m_speedLifts[index], lift) {
        return this.m_speedValues[index];
      }
      index += 1;
    }
    return -1.0;
  }

  // 0 started, 3 already moving, 2 already at that floor, 6 no such lift here
  public func StartLift(lift: EntityID, floor: Int32) -> Int32 {
    let device = GameInstance.FindEntityByID(GetGameInstance(), lift) as LiftDevice;
    if !IsDefined(device) {
      return 6;
    }
    let state = device.GetDevicePS();
    if state.IsMoving() {
      return 3;
    }
    if state.GetActiveFloor() == floor {
      return 2;
    }
    state.NightCoopSetTargetFloor(floor);
    this.m_bypass = true;
    device.SendLiftStartDelayedEvent(floor);
    this.m_bypass = false;
    return 0;
  }

  public func Catches(device: ref<LiftDevice>, floor: Int32) -> Bool {
    if !this.m_oneCab {
      return false;
    }
    if this.m_isHost {
      ArrayPush(this.m_departureLifts, device.GetEntityID());
      ArrayPush(this.m_departureFloors, floor);
      ArrayPush(this.m_departureDelays, device.GetDevicePS().GetLiftStartingDelay());
      ArrayPush(this.m_departureSpeeds, device.GetDevicePS().GetLiftSpeed());
      return false;
    }
    if this.m_bypass || device.GetDevicePS().IsMoving() {
      return false;
    }
    ArrayPush(this.m_requestLifts, device.GetEntityID());
    ArrayPush(this.m_requestFloors, floor);
    return true;
  }
}

@addMethod(LiftControllerPS)
public func NightCoopSetTargetFloor(floor: Int32) {
  this.m_targetFloor = floor;
}

@wrapMethod(LiftDevice)
protected func SendLiftStartDelayedEvent(targetFloorIndex: Int32) {
  let sync = NightCoopLiftSync.Get();
  if IsDefined(sync) && sync.Catches(this, targetFloorIndex) {
    return;
  }
  wrappedMethod(targetFloorIndex);
}

@wrapMethod(LiftDevice)
protected cb func OnLiftStartDelayEvent(evt: ref<LiftStartDelayEvent>) -> Bool {
  let sync = NightCoopLiftSync.Get();
  if IsDefined(sync) {
    if sync.HoldsStart(this, evt) {
      return true;
    }
    sync.BeginStart(this);
  }
  let handled = wrappedMethod(evt);
  if IsDefined(sync) {
    sync.EndStart();
  }
  return handled;
}

@wrapMethod(LiftControllerPS)
public final const func GetLiftSpeed() -> Float {
  let sync = NightCoopLiftSync.Get();
  if IsDefined(sync) {
    let hostSpeed = sync.SpeedFor(this.GetMyEntityID());
    if hostSpeed > 0.0 {
      return hostSpeed;
    }
  }
  return wrappedMethod();
}

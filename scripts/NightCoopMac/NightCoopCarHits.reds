// car hits the script half of the plugins car hits (nightcoops VehicleHit 65): when the car our player drives hits a partner
// body the hit gets queued as a line (peer direction xyz impulse xyz speed) for TakeHits. CarHitUs plays a
// partners car hitting us as the games own OnCarHitPlayer event on our player (knockdown push and damage like any car)

public class NightCoopCarHits extends ScriptableService {
  private let m_hitLines: String;

  public static func Get() -> ref<NightCoopCarHits> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopCarHits") as NightCoopCarHits;
  }

  public func NoteHit(target: ref<GameObject>, evt: ref<gameVehicleHitEvent>) {
    let combat = NightCoopCombat.Get();
    let player = GetPlayer(GetGameInstance()) as PlayerPuppet;
    if !IsDefined(combat) || !IsDefined(player) || !IsDefined(target) {
      return;
    }
    let peer = combat.BodyPeer(target.GetEntityID());
    let car = player.GetMountedVehicle();
    if peer < 0 || !IsDefined(car) || Vector4.Distance(car.GetWorldPosition(), target.GetWorldPosition()) > 8.0 {
      return;
    }
    let relative = evt.vehicleVelocity - evt.preyVelocity;
    let direction = Vector4.Normalize(relative);
    let impulse = relative * 50.0;
    this.m_hitLines += IntToString(peer) + " " + FloatToString(direction.X) + " " +
      FloatToString(direction.Y) + " " + FloatToString(direction.Z) + " " + FloatToString(impulse.X) + " " + FloatToString(impulse.Y) + " " +
      FloatToString(impulse.Z) + " " + FloatToString(Vector4.Length(evt.vehicleVelocity)) + "\n";
  }

  public func TakeHits() -> String {
    let lines = this.m_hitLines;
    this.m_hitLines = "";
    return lines;
  }

  public func CarHitUs(car: EntityID, directionX: Float, directionY: Float, directionZ: Float, impulseX: Float, impulseY: Float, impulseZ: Float) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return false;
    }
    let hit = new OnCarHitPlayer();
    hit.hitDirection = new Vector4(directionX, directionY, directionZ, 0.0);
    hit.seperationImpulse = new Vector4(impulseX, impulseY, impulseZ, 0.0);
    hit.carId = car;
    player.QueueEvent(hit);
    return true;
  }
}

@wrapMethod(GameObject)
protected cb func OnVehicleHit(evt: ref<gameVehicleHitEvent>) -> Bool {
  let handled = wrappedMethod(evt);
  let carHits = NightCoopCarHits.Get();
  if IsDefined(carHits) {
    carHits.NoteHit(this, evt);
  }
  return handled;
}

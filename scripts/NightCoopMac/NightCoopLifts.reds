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

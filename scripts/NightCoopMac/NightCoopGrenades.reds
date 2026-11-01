// grenades the script half of the plugins Grenades (nightcoops physicspush): when one of our players grenades goes off its
// place attack radius and entity id get queued as a line (x y z radius id) for TakeBlasts and the plugin sends them as
// PlayerBlast. PushAt plays a partners blast here as the games physical impulse sphere so loose stuff flies (no damage)

public class NightCoopGrenades extends ScriptableService {
  private let m_blastLines: String;
  private let m_blastIds: array<Uint32>;

  public static func Get() -> ref<NightCoopGrenades> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopGrenades") as NightCoopGrenades;
  }

  public func NoteBlast(grenade: ref<BaseGrenade>) {
    let player = GetPlayer(GetGameInstance());
    let user = grenade.GetUser();
    if !IsDefined(player) || !IsDefined(user) || NotEquals(user.GetEntityID(), player.GetEntityID()) {
      return;
    }
    let id = EntityID.GetHash(grenade.GetEntityID());
    if ArrayContains(this.m_blastIds, id) {
      return;
    }
    ArrayPush(this.m_blastIds, id);
    if ArraySize(this.m_blastIds) > 32 {
      ArrayErase(this.m_blastIds, 0);
    }
    let place = grenade.GetWorldPosition();
    this.m_blastLines += FloatToString(place.X) + " " + FloatToString(place.Y) + " " + FloatToString(place.Z) + " " +
      FloatToString(grenade.GetAttackRadius()) + " " + ToString(id) + "\n";
  }

  public func TakeBlasts() -> String {
    let lines = this.m_blastLines;
    this.m_blastLines = "";
    return lines;
  }

  public func PushAt(x: Float, y: Float, z: Float, radius: Float) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return false;
    }
    let impulse = GameInstance.GetGameEffectSystem(GetGameInstance()).CreateEffectStatic(n"physicalImpulseSphere", n"", player, player);
    if !IsDefined(impulse) {
      return false;
    }
    EffectData.SetVector(impulse.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.position, new Vector4(x, y, z, 1.0));
    EffectData.SetFloat(impulse.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.radius, radius);
    return impulse.Run();
  }
}

@wrapMethod(BaseGrenade)
protected func Detonate(hitNormal: Vector4) {
  wrappedMethod(hitNormal);
  let grenades = NightCoopGrenades.Get();
  if IsDefined(grenades) {
    grenades.NoteBlast(this);
  }
}

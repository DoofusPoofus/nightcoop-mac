// teleport the script half of the plugins quest teleports: moves our player to a point the host offered

public class NightCoopTeleport extends ScriptableService {
  public func MovePlayerTo(x: Float, y: Float, z: Float, yaw: Float) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return false;
    }
    let angles: EulerAngles;
    angles.Yaw = yaw;
    GameInstance.GetTeleportationFacility(GetGameInstance()).Teleport(player, new Vector4(x, y, z, 1.0), angles);
    return true;
  }
}

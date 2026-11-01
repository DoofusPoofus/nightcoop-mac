// photo the script half of the plugins PhotoTogether: the plugin asks evry half second if our photo mode is open

public class NightCoopPhoto extends ScriptableService {
  public func IsActive() -> Bool {
    let photoMode = GameInstance.GetPhotoModeSystem(GetGameInstance());
    return IsDefined(photoMode) && photoMode.IsPhotoModeActive();
  }
}

// braindance the script half of the plugins Braindance (nightcoops braindance host leads the tape): the tape state of the
// braindance we are in (the scenes rewindable section: time length speed direction) and putting a partners tape state on ours

public class NightCoopBraindance extends ScriptableService {
  public func InBraindance() -> Bool {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    return IsDefined(section) && section.IsRewindableSectionActive();
  }

  public func TapeMs() -> Int32 {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    return IsDefined(section) ? Cast<Int32>(section.GetRewindableSectionTimeInSec() * 1000.0) : 0;
  }

  public func LengthMs() -> Int32 {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    return IsDefined(section) ? Cast<Int32>(section.GetRewindableSectionDurationInSec() * 1000.0) : 0;
  }

  public func Speed() -> Int32 {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    return IsDefined(section) ? EnumInt(section.GetRewindableSectionPlaySpeed()) : 2;
  }

  public func Direction() -> Int32 {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    return IsDefined(section) ? EnumInt(section.GetRewindableSectionPlayDirection()) : 0;
  }

  public func Follow(tapeMs: Int32, speed: Int32, direction: Int32) -> Int32 {
    let section = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    if !IsDefined(section) || !section.IsRewindableSectionActive() || section.IsRewindableSectionResetting() {
      return 0;
    }
    let wantedSpeed = IntEnum<scnPlaySpeed>(speed);
    let wantedDirection = IntEnum<scnPlayDirection>(direction);
    let ours = section.GetRewindableSectionTimeInSec();
    let theirs = Cast<Float>(tapeMs) / 1000.0;
    if AbsF(ours - theirs) > 1.0 {
      section.JumpRewindableSection(1.0, theirs, wantedDirection, wantedSpeed);
      return 2;
    }
    if NotEquals(section.GetRewindableSectionPlaySpeed(), wantedSpeed) {
      section.SetRewindableSectionPlaySpeed(wantedSpeed);
    }
    if NotEquals(section.GetRewindableSectionPlayDirection(), wantedDirection) {
      section.SetRewindableSectionPlayDirection(wantedDirection);
    }
    return 1;
  }
}

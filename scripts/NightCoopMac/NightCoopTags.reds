// tags the script half of the plugins NpcTags (nightcoops npctags): evry tag and untag our player makes thru the focus mode
// tagging system gets queued as a line (1 tagged or 0 untagged then the entity id) for TakeTags. ApplyTag tags or untags
// one of our npcs the same way for a partner without queueing it again

public class NightCoopTags extends ScriptableService {
  private let m_tagLines: String;
  private let m_applying: Bool;

  public static func Get() -> ref<NightCoopTags> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopTags") as NightCoopTags;
  }

  public func NoteTag(target: ref<GameObject>, tagged: Bool) {
    if this.m_applying || !IsDefined(target) {
      return;
    }
    this.m_tagLines += (tagged ? "1 " : "0 ") + EntityID.ToDebugStringDecimal(target.GetEntityID()) + "\n";
  }

  public func TakeTags() -> String {
    let lines = this.m_tagLines;
    this.m_tagLines = "";
    return lines;
  }

  public func ApplyTag(target: ref<GameObject>, tagged: Bool) -> Bool {
    let tagging = GameInstance.GetScriptableSystemsContainer(GetGameInstance()).Get(n"FocusModeTaggingSystem") as FocusModeTaggingSystem;
    if !IsDefined(tagging) || !IsDefined(target) {
      return false;
    }
    this.m_applying = true;
    if tagged {
      tagging.TagObject(target);
    } else {
      tagging.UntagObject(target);
    }
    this.m_applying = false;
    return true;
  }
}

@wrapMethod(FocusModeTaggingSystem)
private func TagObject(target: ref<GameObject>) {
  wrappedMethod(target);
  let tags = NightCoopTags.Get();
  if IsDefined(tags) {
    tags.NoteTag(target, true);
  }
}

@wrapMethod(FocusModeTaggingSystem)
private func UntagObject(target: ref<GameObject>) {
  wrappedMethod(target);
  let tags = NightCoopTags.Get();
  if IsDefined(tags) {
    tags.NoteTag(target, false);
  }
}

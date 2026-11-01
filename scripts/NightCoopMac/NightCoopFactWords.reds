// fact words the script half of the plugins FactWords (nightcoops guest fact words): a guest dropping quest items at a drop
// point (kind 1) reading a document on a computer (kind 2) or using a custom device action (kind 3) gets noted so the plugin
// can send the facts that changed right after to the host

public class NightCoopFactWords extends ScriptableService {
  private let m_kinds: array<Int32>;

  public static func Get() -> ref<NightCoopFactWords> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopFactWords") as NightCoopFactWords;
  }

  public func Note(kind: Int32) {
    if !ArrayContains(this.m_kinds, kind) {
      ArrayPush(this.m_kinds, kind);
    }
  }

  public func TestWord() {
    this.Note(2);
    let quests = GameInstance.GetQuestsSystem(GetGameInstance());
    quests.SetFactStr("nightcoop_mac_test_doc", quests.GetFactStr("nightcoop_mac_test_doc") + 7);
  }

  public func TakeKinds() -> array<Int32> {
    let kinds = this.m_kinds;
    ArrayClear(this.m_kinds);
    return kinds;
  }
}

@wrapMethod(DropPointControllerPS)
public func OnDepositQuestItems(evt: ref<DepositQuestItems>) -> EntityNotificationType {
  let words = NightCoopFactWords.Get();
  if IsDefined(words) {
    words.Note(1);
  }
  return wrappedMethod(evt);
}

@wrapMethod(ComputerInkGameController)
protected func ResolveQuestInfo(questInfo: QuestInfo) {
  let words = NightCoopFactWords.Get();
  if IsDefined(words) {
    words.Note(2);
  }
  wrappedMethod(questInfo);
}

@wrapMethod(GenericDevice)
protected cb func OnCustomAction(evt: ref<CustomDeviceAction>) -> Bool {
  let words = NightCoopFactWords.Get();
  if IsDefined(words) {
    words.Note(3);
  }
  return wrappedMethod(evt);
}

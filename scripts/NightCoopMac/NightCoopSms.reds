// sms the script half of the plugins SmsReply (nightcoops smsreply sms_any): a guest in the hosts world does not answer a phone
// chat itself. the chosen reply (its journal entry hash and the question it answers) gets queued for the plugin and the host
// picks it in its own journal like the game does (visited + succeeded) so the shared journal brings it back to evryone

public class NightCoopSms extends ScriptableService {
  private let m_mode: Int32;
  private let m_replyLines: String;

  public static func Get() -> ref<NightCoopSms> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopSms") as NightCoopSms;
  }

  public func Configure(mode: Int32) {
    this.m_mode = mode;
  }

  public func ShouldSwallow(target: ref<ListItemController>, parentHash: Int32) -> Bool {
    if this.m_mode != 2 || !IsDefined(target) {
      return false;
    }
    let data = target.GetData() as JournalEntryListItemData;
    if !IsDefined(data) || !IsDefined(data.m_entry) {
      return false;
    }
    let journal = GameInstance.GetJournalManager(GetGameInstance());
    this.m_replyLines += IntToString(journal.GetEntryHash(data.m_entry)) + " " + IntToString(parentHash) + "\n";
    return true;
  }

  public func TakeReplies() -> String {
    let lines = this.m_replyLines;
    this.m_replyLines = "";
    return lines;
  }

  public func ApplyReply(entryHash: Int32, parentHash: Int32) -> Int32 {
    let journal = GameInstance.GetJournalManager(GetGameInstance());
    let reply = journal.GetEntry(Cast<Uint32>(entryHash));
    let parent = journal.GetEntry(Cast<Uint32>(parentHash));
    if !IsDefined(reply) || !IsDefined(parent) {
      return 4;
    }
    if Equals(journal.GetEntryState(reply), gameJournalEntryState.Succeeded) {
      return 2;
    }
    let messages: array<wref<JournalEntry>>;
    let choices: array<wref<JournalEntry>>;
    journal.GetMessagesAndChoices(parent, messages, choices);
    let open = false;
    for choice in choices {
      if journal.GetEntryHash(choice) == entryHash {
        open = true;
      }
    }
    if !open {
      return 3;
    }
    journal.SetEntryVisited(reply, true);
    journal.ChangeEntryStateByHash(Cast<Uint32>(entryHash), gameJournalEntryState.Succeeded, JournalNotifyOption.Notify);
    return 1;
  }
}

@wrapMethod(MessengerDialogViewController)
private func ActivateReply(target: ref<ListItemController>) {
  let sms = NightCoopSms.Get();
  if IsDefined(sms) && sms.ShouldSwallow(target, this.m_parentHash) {
    return;
  }
  wrappedMethod(target);
}

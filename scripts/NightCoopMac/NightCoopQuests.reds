// quests the script half of the plugins quest sync (src/plugin/QuestSync.cpp nightcoops mirror mode). on the host Watch(true)
// hooks the journal managers state listener evry entry state change lands in m_changedHashes / m_changedStates /
// m_changedNotify and the plugin takes them evry 250 ms (ClearChanges). CollectJournal walks the whole journal (quests meta
// quests contacts tarots internet sites codex briefings and evrything under them) into m_snapshotHashes /
// m_snapshotStates when a guest asks for a snapshot. on a guest ApplyJournal writes one entry state of the host thru
// ChangeEntryStateByHash: 0 changed 1 already like that 2 no such entry here

public class NightCoopQuestSync extends ScriptableService {
  private let m_watching: Bool;
  private let m_watchedJournal: wref<JournalManager>;
  private let m_changedHashes: array<Uint32>;
  private let m_changedStates: array<Int32>;
  private let m_changedNotify: array<Bool>;
  private let m_snapshotHashes: array<Uint32>;
  private let m_snapshotStates: array<Int32>;

  public static func Get() -> ref<NightCoopQuestSync> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopQuestSync") as NightCoopQuestSync;
  }

  private func Journal() -> ref<JournalManager> {
    return GameInstance.GetJournalManager(GetGameInstance());
  }

  public func Watch(on: Bool) {
    if this.m_watching {
      if on {
        return;
      }
      if IsDefined(this.m_watchedJournal) {
        this.m_watchedJournal.UnregisterScriptCallback(this, n"OnJournalStateChanged");
      }
      this.m_watching = false;
      this.ClearChanges();
      return;
    }
    if !on {
      return;
    }
    let journal = this.Journal();
    if !IsDefined(journal) {
      return;
    }
    journal.UnregisterScriptCallback(this, n"OnJournalStateChanged");
    journal.RegisterScriptCallback(this, n"OnJournalStateChanged", gameJournalListenerType.State);
    this.m_watchedJournal = journal;
    this.m_watching = true;
  }

  public func ClearChanges() {
    ArrayClear(this.m_changedHashes);
    ArrayClear(this.m_changedStates);
    ArrayClear(this.m_changedNotify);
  }

  protected cb func OnJournalStateChanged(hash: Uint32, className: CName, notifyOption: JournalNotifyOption, changeType: JournalChangeType) -> Bool {
    let journal = this.Journal();
    if !this.m_watching || !IsDefined(journal) {
      return false;
    }
    let entry = journal.GetEntry(hash);
    if !IsDefined(entry) {
      return false;
    }
    ArrayPush(this.m_changedHashes, hash);
    ArrayPush(this.m_changedStates, EnumInt(journal.GetEntryState(entry)));
    ArrayPush(this.m_changedNotify, Equals(notifyOption, JournalNotifyOption.Notify));
    return true;
  }

  public func TrackedHash() -> Uint32 {
    let journal = this.Journal();
    if !IsDefined(journal) {
      return 0u;
    }
    let tracked = journal.GetTrackedEntry();
    if !IsDefined(tracked) {
      return 0u;
    }
    return Cast<Uint32>(journal.GetEntryHash(tracked));
  }

  public func CollectJournal() -> Int32 {
    ArrayClear(this.m_snapshotHashes);
    ArrayClear(this.m_snapshotStates);
    let journal = this.Journal();
    if !IsDefined(journal) {
      return 0;
    }
    let everyState: JournalRequestStateFilter;
    everyState.inactive = true;
    everyState.active = true;
    everyState.succeeded = true;
    everyState.failed = true;
    let everything: JournalRequestContext;
    everything.stateFilter = everyState;
    let quests: array<wref<JournalEntry>>;
    journal.GetQuests(everything, quests);
    this.Walk(journal, everyState, quests);
    let metaQuests: array<wref<JournalEntry>>;
    journal.GetMetaQuests(everything, metaQuests);
    this.Walk(journal, everyState, metaQuests);
    let contacts: array<wref<JournalEntry>>;
    journal.GetContacts(everything, contacts);
    this.Walk(journal, everyState, contacts);
    let tarots: array<wref<JournalEntry>>;
    journal.GetTarots(everything, tarots);
    this.Walk(journal, everyState, tarots);
    let internetSites: array<wref<JournalEntry>>;
    journal.GetInternetSites(everything, internetSites);
    this.Walk(journal, everyState, internetSites);
    let codex: array<wref<JournalEntry>>;
    journal.GetCodexCategories(everything, codex);
    this.Walk(journal, everyState, codex);
    let briefings: array<wref<JournalEntry>>;
    journal.GetBriefings(everything, briefings);
    this.Walk(journal, everyState, briefings);
    return ArraySize(this.m_snapshotHashes);
  }

  private func Walk(journal: ref<JournalManager>, everyState: JournalRequestStateFilter, roots: array<wref<JournalEntry>>) {
    let waiting = roots;
    while ArraySize(waiting) > 0 {
      let entry = ArrayPop(waiting);
      if IsDefined(entry) {
        ArrayPush(this.m_snapshotHashes, Cast<Uint32>(journal.GetEntryHash(entry)));
        ArrayPush(this.m_snapshotStates, EnumInt(journal.GetEntryState(entry)));
        let children: array<wref<JournalEntry>>;
        journal.GetChildren(entry, everyState, children);
        for child in children {
          ArrayPush(waiting, child);
        }
      }
    }
  }

  public func ApplyJournal(hash: Uint32, state: Int32, notify: Bool) -> Int32 {
    let journal = this.Journal();
    if !IsDefined(journal) {
      return 2;
    }
    let entry = journal.GetEntry(hash);
    if !IsDefined(entry) {
      return 2;
    }
    let wanted = IntEnum<gameJournalEntryState>(state);
    if Equals(journal.GetEntryState(entry), wanted) {
      return 1;
    }
    let notifyOption = notify ? JournalNotifyOption.Notify : JournalNotifyOption.DoNotNotify;
    journal.ChangeEntryStateByHash(hash, wanted, notifyOption);
    return 0;
  }
}

// world handoff the script half of the plugins WorldHandoff (nightcoops sessionflow world parts): the systems request
// handler gets taken from the onscreen message controller or the in game menu (or codewares GetSystemRequestsHandler). the host asks for a manual save with it
// (HasFreeSaveSlot + ManualSave like nightcoops RequestManualSave with the folder name the plugin picked. the game still
// writes into ManualSave-0 so the plugin guards that slot. a game started straight into a save has no save list so
// HasFreeSaveSlot says no: the list gets asked for once and the third try saves anyway) a guest loads the folder the plugin wrote: the save list
// gets asked for (RequestSavesForLoad) the folder is looked up by name in it and loaded by its index (LoadSavedGame)
// when the list does not answer the plugin loads the newest save instead (LoadNewest: the in game menus own quick load
// DelayedHandleQuickLoad or LoadLastCheckpoint(false) without that menu) then LoadIndex(0) (the save list may be filled even
// when our callback never comes and our save is the newest) and as the last try LoadCheckpoint
// loadState: 0 nothing 1 the list was asked for 2 the load started 3 the folder was not in the list

public class NightCoopWorldHandoff extends ScriptableService {
  private let m_handler: wref<inkISystemRequestsHandler>;
  private let m_menu: wref<gameuiInGameMenuGameController>;
  private let m_loadFolder: String;
  private let m_loadState: Int32;
  private let m_listedSaves: Int32;
  private let m_listSample: String;
  private let m_saveListAsked: Bool;
  private let m_noSlotTries: Int32;

  public static func Get() -> ref<NightCoopWorldHandoff> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopWorldHandoff") as NightCoopWorldHandoff;
  }

  public func Capture(handler: wref<inkISystemRequestsHandler>) {
    if IsDefined(handler) {
      this.m_handler = handler;
    }
  }

  public func CaptureMenu(menu: wref<gameuiInGameMenuGameController>) {
    this.m_menu = menu;
  }

  private func Handler() -> wref<inkISystemRequestsHandler> {
    if !IsDefined(this.m_handler) {
      this.m_handler = GameInstance.GetSystemRequestsHandler();
    }
    return this.m_handler;
  }

  public func HasHandler() -> Bool {
    return IsDefined(this.Handler());
  }

  public func MakeSave(folder: String) -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) || StrLen(folder) == 0 {
      return false;
    }
    if !handler.HasFreeSaveSlot(folder) {
      if !this.m_saveListAsked {
        this.m_saveListAsked = true;
        handler.RequestSavesForSave();
      }
      this.m_noSlotTries += 1;
      if this.m_noSlotTries < 3 {
        return false;
      }
    }
    this.m_noSlotTries = 0;
    handler.ManualSave(folder);
    return true;
  }

  public func LoadFolder(folder: String) -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) {
      return false;
    }
    this.m_loadFolder = folder;
    this.m_loadState = 1;
    handler.RegisterToCallback(n"OnSavesForLoadReady", this, n"OnNightCoopSavesListed");
    handler.RequestSavesForLoad();
    return true;
  }

  public func LoadNewest() -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) {
      return false;
    }
    handler.UnregisterFromCallback(n"OnSavesForLoadReady", this, n"OnNightCoopSavesListed");
    this.m_loadState = 2;
    let menu = this.m_menu;
    if IsDefined(menu) {
      menu.NightCoopQuickLoad();
    } else {
      handler.LoadLastCheckpoint(false);
    }
    return true;
  }

  public func LoadIndex(index: Int32) -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) {
      return false;
    }
    this.m_loadState = 2;
    handler.LoadSavedGame(index);
    return true;
  }

  public func LoadCheckpoint() -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) {
      return false;
    }
    this.m_loadState = 2;
    handler.LoadLastCheckpoint(false);
    return true;
  }

  public func HasMenu() -> Bool {
    return IsDefined(this.m_menu);
  }

  public func LoadState() -> Int32 {
    return this.m_loadState;
  }

  public func SaveBlockReason(folder: String) -> String {
    let handler = this.Handler();
    if !IsDefined(handler) {
      return "no save handler yet";
    }
    let locks: array<gameSaveLock>;
    if GameInstance.IsSavingLocked(GetGameInstance(), locks) {
      let reasons = "";
      for lock in locks {
        reasons += EnumValueToString("gameSaveLockReason", Cast<Int64>(EnumInt(lock.reason))) + " ";
      }
      return "saving is locked: " + reasons;
    }
    if !handler.HasFreeSaveSlot(folder) {
      return "no free save slot for " + folder;
    }
    return "";
  }

  public func TestAutosave() -> Bool {
    let player = GetPlayer(GetGameInstance());
    return IsDefined(player) && player.RequestAutoSave(true);
  }

  public func ListedSaves() -> Int32 {
    return this.m_listedSaves;
  }

  public func ListSample() -> String {
    return this.m_listSample;
  }

  protected cb func OnNightCoopSavesListed(saves: array<String>) -> Bool {
    let handler = this.Handler();
    if !IsDefined(handler) || this.m_loadState != 1 {
      return false;
    }
    handler.UnregisterFromCallback(n"OnSavesForLoadReady", this, n"OnNightCoopSavesListed");
    this.m_listedSaves = ArraySize(saves);
    this.m_listSample = "";
    let shown = 0;
    while shown < ArraySize(saves) && shown < 4 {
      this.m_listSample += saves[shown] + " | ";
      shown += 1;
    }
    let index = 0;
    while index < ArraySize(saves) {
      if Equals(saves[index], this.m_loadFolder) {
        this.m_loadState = 2;
        handler.LoadSavedGame(index);
        return true;
      }
      index += 1;
    }
    this.m_loadState = 3;
    return true;
  }
}

@addMethod(gameuiInGameMenuGameController)
public func NightCoopQuickLoad() {
  this.DelayedHandleQuickLoad();
}

@wrapMethod(gameuiInGameMenuGameController)
protected cb func OnInitialize() -> Bool {
  let handled = wrappedMethod();
  let handoff = NightCoopWorldHandoff.Get();
  if IsDefined(handoff) {
    handoff.Capture(this.GetSystemRequestsHandler());
    handoff.CaptureMenu(this);
  }
  return handled;
}

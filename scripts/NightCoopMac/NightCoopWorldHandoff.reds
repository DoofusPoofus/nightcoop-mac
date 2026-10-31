// world handoff, the script half of the plugins WorldHandoff (nightcoops sessionflow world parts): the systems request
// handler gets taken from the onscreen message controller once the hud is up. the host asks for a manual save with it
// (HasFreeSaveSlot + ManualSave like nightcoops RequestManualSave), a guest loads the folder the plugin wrote: the save list
// gets asked for (RequestSavesForLoad), the folder is looked up by name in it and loaded by its index (LoadSavedGame)
// when the list does not answer the plugin loads the newest save instead (LoadNewest: the games own quick load thru the in
// game menu controller like nightcoop, LoadLastCheckpoint(false) without that controller)
// loadState: 0 nothing, 1 the list was asked for, 2 the load started, 3 the folder was not in the list

public class NightCoopWorldHandoff extends ScriptableService {
  private let m_handler: wref<inkISystemRequestsHandler>;
  private let m_menu: wref<gameuiInGameMenuGameController>;
  private let m_loadFolder: String;
  private let m_loadState: Int32;
  private let m_listedSaves: Int32;
  private let m_listSample: String;

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

  public func HasHandler() -> Bool {
    return IsDefined(this.m_handler);
  }

  public func MakeSave() -> Bool {
    let handler = this.m_handler;
    if !IsDefined(handler) || !handler.HasFreeSaveSlot("ManualSave-") {
      return false;
    }
    handler.ManualSave("ManualSave-");
    return true;
  }

  public func LoadFolder(folder: String) -> Bool {
    let handler = this.m_handler;
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
    let handler = this.m_handler;
    if !IsDefined(handler) {
      return false;
    }
    handler.UnregisterFromCallback(n"OnSavesForLoadReady", this, n"OnNightCoopSavesListed");
    this.m_loadState = 2;
    let menu = this.m_menu;
    if IsDefined(menu) {
      handler.RegisterToCallback(n"OnSavesForLoadReady", menu, n"OnQuickLoadSavesReady");
      handler.RequestSavesForLoad();
    } else {
      handler.LoadLastCheckpoint(false);
    }
    return true;
  }

  public func LoadState() -> Int32 {
    return this.m_loadState;
  }

  public func ListedSaves() -> Int32 {
    return this.m_listedSaves;
  }

  public func ListSample() -> String {
    return this.m_listSample;
  }

  protected cb func OnNightCoopSavesListed(saves: array<String>) -> Bool {
    let handler = this.m_handler;
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

// dialogs and scenes for the plugin (DialogScenes.cpp) like nightcoops dialogvote and sceneskip do it on windows
// Refresh reads the UIInteractions blackboard (DialogChoiceHubs ActiveChoiceHubID SelectedIndex): the active choice window
// lands in hubOpen hubId hubTitle rowTexts rowTypes selectedIndex and the plugin reads those fields thru rtti
// it also listens to UIGameData.ShowDialogLine: evry subtitle line we see lands in the line arrays (the host relays them as
// DialogLine) and ownLinesSeen counts them so a guest knows when its own scene talks. ShowRelayedLine puts a hosts line on
// our subtitles and HideRelayedLine takes it off again those never count as our own
// SetBodyHidden turns the visual components of a partner body and of the items in its slots off and remembers them showing
// turns only those back on (the body builder keeps some parts off on purpose)
// skipActive skipAllowed mirror the scene fast forward and StartSkip StopSkip run it for a partner that skips
// there is no script call that picks a dialog choice so the plugin presses the keys for that

public class NightCoopDialogs extends ScriptableService {
  private let m_hubOpen: Bool;
  private let m_hubId: Int32;
  private let m_hubTitle: String;
  private let m_hubTimed: Bool;
  private let m_hubSeconds: Float;
  private let m_selectedIndex: Int32;
  private let m_rowTexts: array<String>;
  private let m_rowTypes: array<Int32>;
  private let m_lineIds: array<CRUID>;
  private let m_lineTypes: array<Int32>;
  private let m_lineSeconds: array<Float>;
  private let m_lineSpeakers: array<String>;
  private let m_lineTexts: array<String>;
  private let m_ownLinesSeen: Int32;
  private let m_language: String;
  private let m_relayedIds: array<CRUID>;
  private let m_lineBoard: ref<IBlackboard>;
  private let m_lineCallback: ref<CallbackHandle>;
  private let m_hiddenOwners: array<EntityID>;
  private let m_hiddenParts: array<wref<IComponent>>;
  private let m_skipActive: Bool;
  private let m_skipAllowed: Bool;

  public func Refresh() {
    this.ReadChoiceWindow();
    this.WatchLines();
    this.ReadSkip();
  }

  public func ClearLines() {
    ArrayClear(this.m_lineIds);
    ArrayClear(this.m_lineTypes);
    ArrayClear(this.m_lineSeconds);
    ArrayClear(this.m_lineSpeakers);
    ArrayClear(this.m_lineTexts);
  }

  public func ShowRelayedLine(id: CRUID, speaker: String, text: String, lineType: Int32, seconds: Float) {
    let board = GameInstance.GetBlackboardSystem(GetGameInstance()).Get(GetAllBlackboardDefs().UIGameData);
    if !IsDefined(board) {
      return;
    }
    let line: scnDialogLineData;
    line.id = id;
    line.text = text;
    line.type = IntEnum<scnDialogLineType>(lineType);
    line.speakerName = speaker;
    line.duration = seconds;
    let lines: array<scnDialogLineData>;
    ArrayPush(lines, line);
    ArrayPush(this.m_relayedIds, id);
    board.SetVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, ToVariant(lines), true);
  }

  public func HideRelayedLine(id: CRUID) {
    ArrayRemove(this.m_relayedIds, id);
    let board = GameInstance.GetBlackboardSystem(GetGameInstance()).Get(GetAllBlackboardDefs().UIGameData);
    if !IsDefined(board) {
      return;
    }
    let ids: array<CRUID>;
    ArrayPush(ids, id);
    board.SetVariant(GetAllBlackboardDefs().UIGameData.HideDialogLine, ToVariant(ids), true);
  }

  public func SetBodyHidden(body: ref<Entity>, hidden: Bool) -> Int32 {
    if !IsDefined(body) {
      return 0;
    }
    let owner = body.GetEntityID();
    if !hidden {
      return this.ShowParts(owner);
    }
    let turnedOff = this.HideParts(body, owner);
    let puppet = body as GameObject;
    let items: array<wref<gameItemData>>;
    let transactions = GameInstance.GetTransactionSystem(GetGameInstance());
    if IsDefined(puppet) && transactions.GetItemList(puppet, items) {
      for item in items {
        let worn = transactions.GetItemInSlotByItemID(puppet, item.GetID());
        if IsDefined(worn) {
          turnedOff += this.HideParts(worn, owner);
        }
      }
    }
    return turnedOff;
  }

  public func StartSkip() -> Bool {
    let scenes = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    if !IsDefined(scenes) || !scenes.IsFastForwardingAllowed(IntEnum<scnFastForwardMode>(0)) {
      return false;
    }
    if !scenes.IsFastForwardingActive(IntEnum<scnFastForwardMode>(0)) {
      scenes.FastForwardingActivate(IntEnum<scnFastForwardMode>(0));
    }
    return true;
  }

  public func StopSkip() {
    let scenes = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    if IsDefined(scenes) {
      scenes.FastForwardingDeactivate();
    }
  }

  protected cb func OnShowDialogLine(value: Variant) -> Bool {
    let lines = FromVariant<array<scnDialogLineData>>(value);
    for line in lines {
      if !ArrayContains(this.m_relayedIds, line.id) {
        this.m_ownLinesSeen += 1;
        ArrayPush(this.m_lineIds, line.id);
        ArrayPush(this.m_lineTypes, EnumInt(line.type));
        ArrayPush(this.m_lineSeconds, line.duration);
        ArrayPush(this.m_lineSpeakers, line.speakerName);
        ArrayPush(this.m_lineTexts, line.text);
      }
    }
    if ArraySize(this.m_lineIds) > 64 {
      this.ClearLines();
    }
    return false;
  }

  private func ReadChoiceWindow() {
    this.m_hubOpen = false;
    ArrayClear(this.m_rowTexts);
    ArrayClear(this.m_rowTypes);
    let defs = GetAllBlackboardDefs().UIInteractions;
    let board = GameInstance.GetBlackboardSystem(GetGameInstance()).Get(defs);
    if !IsDefined(board) {
      return;
    }
    let activeHub = board.GetInt(defs.ActiveChoiceHubID);
    this.m_selectedIndex = board.GetInt(defs.SelectedIndex);
    let hubs = FromVariant<DialogChoiceHubs>(board.GetVariant(defs.DialogChoiceHubs));
    for hub in hubs.choiceHubs {
      if hub.id == activeHub && ArraySize(hub.choices) > 0 {
        this.m_hubOpen = true;
        this.m_hubId = hub.id;
        this.m_hubTitle = hub.title;
        this.m_hubTimed = IsDefined(hub.timeProvider);
        this.m_hubSeconds = this.m_hubTimed ? hub.timeProvider.GetDuration() : 0.0;
        for choice in hub.choices {
          ArrayPush(this.m_rowTexts, choice.localizedName);
          ArrayPush(this.m_rowTypes, Cast<Int32>(choice.type.properties));
        }
      }
    }
  }

  private func WatchLines() {
    let board = GameInstance.GetBlackboardSystem(GetGameInstance()).Get(GetAllBlackboardDefs().UIGameData);
    if !IsDefined(board) || (IsDefined(this.m_lineBoard) && Equals(this.m_lineBoard, board)) {
      return;
    }
    this.m_lineBoard = board;
    this.m_lineCallback = board.RegisterListenerVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, this, n"OnShowDialogLine");
    let onScreen = GameInstance.GetSettingsSystem(GetGameInstance()).GetVar(n"/language", n"OnScreen") as ConfigVarListName;
    if IsDefined(onScreen) {
      this.m_language = NameToString(onScreen.GetValue());
    }
  }

  private func ReadSkip() {
    let scenes = GameInstance.GetSceneSystem(GetGameInstance()).GetScriptInterface();
    this.m_skipActive = IsDefined(scenes) && scenes.IsFastForwardingActive(IntEnum<scnFastForwardMode>(0));
    this.m_skipAllowed = IsDefined(scenes) && scenes.IsFastForwardingAllowed(IntEnum<scnFastForwardMode>(0));
  }

  private func HideParts(entity: ref<Entity>, owner: EntityID) -> Int32 {
    let turnedOff = 0;
    for component in entity.GetComponents() {
      let visual = component as IVisualComponent;
      if IsDefined(visual) && visual.IsEnabled() {
        visual.Toggle(false);
        ArrayPush(this.m_hiddenOwners, owner);
        ArrayPush(this.m_hiddenParts, visual);
        turnedOff += 1;
      }
    }
    return turnedOff;
  }

  private func ShowParts(owner: EntityID) -> Int32 {
    let turnedOn = 0;
    let index = ArraySize(this.m_hiddenParts) - 1;
    while index >= 0 {
      let part = this.m_hiddenParts[index];
      if !IsDefined(part) || Equals(this.m_hiddenOwners[index], owner) {
        if IsDefined(part) {
          part.Toggle(true);
          turnedOn += 1;
        }
        ArrayErase(this.m_hiddenParts, index);
        ArrayErase(this.m_hiddenOwners, index);
      }
      index -= 1;
    }
    return turnedOn;
  }
}

// partner pins on the minimap and the world map, the script half of the plugins PartnerPins (nightcoops playermarkers)
// the plugin registers one CPO_RemotePlayerVariant mappin per partner, the games ui has no proper look for that leftover
// multiplayer variant so these wraps give it one like nightcoop does: the minimap shows it with the poi widget and the quest
// runtime profile, the 3d world container shows nothing (nightcoop draws its own plates there), the icon is codex_character
// and its texts say Partner. evry minimap profile made for one gets counted in the session flow service so the plugin can log it

@wrapMethod(MinimapContainerController)
protected func CreateMappinUIProfile(mappin: wref<IMappin>, mappinVariant: gamedataMappinVariant, customData: ref<MappinControllerCustomData>) -> MappinUIProfile {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    NightCoopSessionFlow.NotePartnerPinProfile();
    return MappinUIProfile.Create(r"base\\gameplay\\gui\\widgets\\minimap\\minimap_poi_mappin.inkwidget", t"MappinUISpawnProfile.Always", t"MinimapMappinUIProfile.Quest");
  }
  return wrappedMethod(mappin, mappinVariant, customData);
}

@wrapMethod(WorldMappinsContainerController)
protected func CreateMappinUIProfile(mappin: wref<IMappin>, mappinVariant: gamedataMappinVariant, customData: ref<MappinControllerCustomData>) -> MappinUIProfile {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return MappinUIProfile.None();
  }
  return wrappedMethod(mappin, mappinVariant, customData);
}

@wrapMethod(MappinUIUtils)
public final static func MappinToTexturePart(mappinVariant: gamedataMappinVariant) -> CName {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"codex_character";
  }
  return wrappedMethod(mappinVariant);
}

@wrapMethod(MappinUIUtils)
public final static func MappinToTexturePart(mappinVariant: gamedataMappinVariant, phase: gamedataMappinPhase) -> CName {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"codex_character";
  }
  return wrappedMethod(mappinVariant, phase);
}

@wrapMethod(MappinUIUtils)
public final static func MappinToString(mappinVariant: gamedataMappinVariant) -> CName {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"Partner";
  }
  return wrappedMethod(mappinVariant);
}

@wrapMethod(MappinUIUtils)
public final static func MappinToString(mappinVariant: gamedataMappinVariant, phase: gamedataMappinPhase) -> CName {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"Partner";
  }
  return wrappedMethod(mappinVariant, phase);
}

@wrapMethod(MappinUIUtils)
public final static func MappinToDescriptionString(mappinVariant: gamedataMappinVariant) -> CName {
  if Equals(mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"Co-op partner";
  }
  return wrappedMethod(mappinVariant);
}

@wrapMethod(MinimapPOIMappinController)
protected func ComputeRootState() -> CName {
  if Equals(this.m_mappinVariant, gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"InteractionDefault";
  }
  return wrappedMethod();
}

@wrapMethod(BaseWorldMapMappinController)
protected func ComputeRootState() -> CName {
  let mappin = this.GetMappin();
  if IsDefined(mappin) && Equals(mappin.GetVariant(), gamedataMappinVariant.CPO_RemotePlayerVariant) {
    return n"InteractionDefault";
  }
  return wrappedMethod();
}

// name plates over partner heads (nightcoops world markers): a NightCoopPlayerMarkers canvas sits on top of the hud window,
// found by walking up from the onscreen message controller, and holds one NightCoopPlayerMarker per partner with their
// name (36 px) and distance (28 px) in the hud cyan. the plugin projects each partner to the screen evry frame and calls
// BeginFrame, Place for evry partner it sees, then EndFrame, which hides the plates nobody placed. coordinates are in the
// 3840 x 2160 hud space
public class NightCoopPartnerPlates extends ScriptableService {
  private let m_layer: wref<inkCanvas>;
  private let m_peers: array<Int32>;
  private let m_plates: array<wref<inkCanvas>>;
  private let m_placed: array<Bool>;
  private let m_platesMade: Int32;

  public static func Get() -> ref<NightCoopPartnerPlates> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopPartnerPlates") as NightCoopPartnerPlates;
  }

  public func AttachTo(widget: wref<inkWidget>) {
    if IsDefined(this.m_layer) || !IsDefined(widget) {
      return;
    }
    let top = widget;
    while IsDefined(top.GetParentWidget()) {
      top = top.GetParentWidget();
    }
    let window = top as inkCompoundWidget;
    if !IsDefined(window) {
      return;
    }
    let layer = new inkCanvas();
    layer.SetName(n"NightCoopPlayerMarkers");
    layer.SetAnchor(inkEAnchor.TopLeft);
    layer.SetSize(3840.0, 2160.0);
    layer.SetInteractive(false);
    window.AddChildWidget(layer);
    this.m_layer = layer;
    ArrayClear(this.m_peers);
    ArrayClear(this.m_plates);
    ArrayClear(this.m_placed);
  }

  public func HasLayer() -> Bool {
    return IsDefined(this.m_layer);
  }

  public func PlatesMade() -> Int32 {
    return this.m_platesMade;
  }

  public func BeginFrame() {
    let index = 0;
    while index < ArraySize(this.m_placed) {
      this.m_placed[index] = false;
      index += 1;
    }
  }

  public func Place(peer: Int32, name: String, x: Float, y: Float, scale: Float, opacity: Float, distance: String) {
    if !IsDefined(this.m_layer) {
      return;
    }
    let index = ArrayFindFirst(this.m_peers, peer);
    if index < 0 || !IsDefined(this.m_plates[index]) {
      let plate = this.MakePlate();
      if index < 0 {
        ArrayPush(this.m_peers, peer);
        ArrayPush(this.m_plates, plate);
        ArrayPush(this.m_placed, false);
        index = ArraySize(this.m_peers) - 1;
      } else {
        this.m_plates[index] = plate;
      }
    }
    let plate = this.m_plates[index];
    this.m_placed[index] = true;
    plate.SetTranslation(x, y);
    plate.SetScale(new Vector2(scale, scale));
    plate.SetOpacity(opacity);
    plate.SetVisible(true);
    (plate.GetWidgetByPathName(n"name") as inkText).SetText(name);
    (plate.GetWidgetByPathName(n"distance") as inkText).SetText(distance);
  }

  public func EndFrame() {
    let index = 0;
    while index < ArraySize(this.m_plates) {
      if !this.m_placed[index] && IsDefined(this.m_plates[index]) {
        this.m_plates[index].SetVisible(false);
      }
      index += 1;
    }
  }

  private func MakePlate() -> ref<inkCanvas> {
    let plate = new inkCanvas();
    plate.SetName(n"NightCoopPlayerMarker");
    plate.SetAnchor(inkEAnchor.TopLeft);
    plate.SetAnchorPoint(0.5, 1.0);
    plate.SetSize(1100.0, 320.0);
    plate.SetInteractive(false);
    let cyan = new HDRColor(0.369, 0.965, 1.0, 1.0);
    let name = new inkText();
    name.SetName(n"name");
    name.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
    name.SetFontSize(36);
    name.SetTintColor(cyan);
    name.SetAnchor(inkEAnchor.BottomCenter);
    name.SetAnchorPoint(0.5, 1.0);
    name.SetHorizontalAlignment(textHorizontalAlignment.Center);
    name.SetFitToContent(true);
    name.SetTranslation(0.0, -110.0);
    plate.AddChildWidget(name);
    let distance = new inkText();
    distance.SetName(n"distance");
    distance.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
    distance.SetFontSize(28);
    distance.SetTintColor(cyan);
    distance.SetOpacity(0.8);
    distance.SetAnchor(inkEAnchor.BottomCenter);
    distance.SetAnchorPoint(0.5, 1.0);
    distance.SetHorizontalAlignment(textHorizontalAlignment.Center);
    distance.SetFitToContent(true);
    distance.SetTranslation(0.0, -70.0);
    plate.AddChildWidget(distance);
    this.m_layer.AddChildWidget(plate);
    this.m_platesMade += 1;
    return plate;
  }
}

@wrapMethod(OnscreenMessageGameController)
protected cb func OnInitialize() -> Bool {
  let handled = wrappedMethod();
  let plates = NightCoopPartnerPlates.Get();
  if IsDefined(plates) {
    plates.AttachTo(this.GetRootWidget());
  }
  return handled;
}

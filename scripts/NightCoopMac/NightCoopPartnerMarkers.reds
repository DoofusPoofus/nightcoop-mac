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

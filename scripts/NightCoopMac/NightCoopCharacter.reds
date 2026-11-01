// character the script half of the handoff character carry over (level and perks are in the scriptable systems blob of the
// save which the save package can not split yet so they go around it)
// Capture writes our V as lines: level <proficiency> <level> | attr <stat type> <value> | perk <new perk> <level> | points
// <development point type> <unspent>. Apply in the hosts world resets perks and attributes sets evry proficiency level adds
// points buys the attributes up to their values unlocks and buys the perks (forced) and sets the unspent points to ours.
// it returns how many values ended up right. SlotCount / GetEquipped / EquipNow let the plugin carry the equipped item ids too

public class NightCoopCharacter extends ScriptableService {
  public static func Get() -> ref<NightCoopCharacter> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopCharacter") as NightCoopCharacter;
  }

  private func Data() -> ref<PlayerDevelopmentData> {
    let player = GetPlayer(GetGameInstance());
    let system = GameInstance.GetScriptableSystemsContainer(GetGameInstance()).Get(n"PlayerDevelopmentSystem") as PlayerDevelopmentSystem;
    if !IsDefined(player) || !IsDefined(system) {
      return null;
    }
    return system.GetDevelopmentData(player);
  }

  private func AttributeTypes() -> array<gamedataStatType> {
    return [gamedataStatType.Strength, gamedataStatType.Reflexes, gamedataStatType.TechnicalAbility, gamedataStatType.Intelligence, gamedataStatType.Cool];
  }

  private func AttributeValue(stat: gamedataStatType) -> Int32 {
    let player = GetPlayer(GetGameInstance());
    return Cast<Int32>(GameInstance.GetStatsSystem(GetGameInstance()).GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), stat));
  }

  public func SlotCount(area: Int32) -> Int32 {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return 0;
    }
    let data = EquipmentSystem.GetData(player);
    let areaType = IntEnum<gamedataEquipmentArea>(area);
    if !IsDefined(data) || data.GetEquipAreaIndex(areaType) < 0 {
      return 0;
    }
    return ArraySize(data.GetEquipArea(areaType).equipSlots);
  }

  public func GetEquipped(area: Int32, slot: Int32) -> ItemID {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return ItemID.None();
    }
    return EquipmentSystem.GetInstance(player).GetItemInEquipSlot(player, IntEnum<gamedataEquipmentArea>(area), slot);
  }

  public func EquipNow(area: Int32, slot: Int32, item: ItemID) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) || !ItemID.IsValid(item) || !GameInstance.GetTransactionSystem(GetGameInstance()).HasItem(player, item) {
      return false;
    }
    let request = new EquipRequest();
    request.owner = player;
    request.itemID = item;
    request.slotIndex = slot;
    EquipmentSystem.GetInstance(player).QueueRequest(request);
    return true;
  }

  public func Capture() -> String {
    let data = this.Data();
    if !IsDefined(data) {
      return "";
    }
    let text = "";
    let proficiency = 0;
    while proficiency < EnumInt(gamedataProficiencyType.Count) {
      text += "level " + IntToString(proficiency) + " " + IntToString(data.GetProficiencyLevel(IntEnum<gamedataProficiencyType>(proficiency))) + "\n";
      proficiency += 1;
    }
    for stat in this.AttributeTypes() {
      text += "attr " + IntToString(EnumInt(stat)) + " " + IntToString(this.AttributeValue(stat)) + "\n";
    }
    let perk = 0;
    while perk < EnumInt(gamedataNewPerkType.Count) {
      let level = data.IsNewPerkBought(IntEnum<gamedataNewPerkType>(perk));
      if level > 0 {
        text += "perk " + IntToString(perk) + " " + IntToString(level) + "\n";
      }
      perk += 1;
    }
    let points = 0;
    while points < EnumInt(gamedataDevelopmentPointType.Count) {
      text += "points " + IntToString(points) + " " + IntToString(data.GetDevPoints(IntEnum<gamedataDevelopmentPointType>(points))) + "\n";
      points += 1;
    }
    return text;
  }

  public func Apply(text: String) -> String {
    let data = this.Data();
    if !IsDefined(data) || StrLen(text) == 0 {
      return "no development data";
    }
    let levels: array<Int32>;
    let levelTypes: array<Int32>;
    let attributeTypes: array<Int32>;
    let attributeValues: array<Int32>;
    let perkTypes: array<Int32>;
    let perkLevels: array<Int32>;
    let pointTypes: array<Int32>;
    let pointValues: array<Int32>;
    for line in StrSplit(text, "\n") {
      let words = StrSplit(line, " ");
      if ArraySize(words) == 3 {
        let first = StringToInt(words[1]);
        let second = StringToInt(words[2]);
        if Equals(words[0], "level") {
          ArrayPush(levelTypes, first);
          ArrayPush(levels, second);
        } else if Equals(words[0], "attr") {
          ArrayPush(attributeTypes, first);
          ArrayPush(attributeValues, second);
        } else if Equals(words[0], "perk") {
          ArrayPush(perkTypes, first);
          ArrayPush(perkLevels, second);
        } else if Equals(words[0], "points") {
          ArrayPush(pointTypes, first);
          ArrayPush(pointValues, second);
        }
      }
    }
    data.ResetNewPerks();
    data.ResetAttributes();
    let index = 0;
    while index < ArraySize(levelTypes) {
      let proficiency = IntEnum<gamedataProficiencyType>(levelTypes[index]);
      if NotEquals(proficiency, gamedataProficiencyType.Count) && levels[index] > 0 && data.GetProficiencyLevel(proficiency) != levels[index] {
        data.SetLevel(proficiency, levels[index], IntEnum<telemetryLevelGainReason>(0), true);
      }
      index += 1;
    }
    data.AddDevelopmentPoints(200, gamedataDevelopmentPointType.Attribute);
    index = 0;
    while index < ArraySize(attributeTypes) {
      let stat = IntEnum<gamedataStatType>(attributeTypes[index]);
      let tries = 0;
      while this.AttributeValue(stat) < attributeValues[index] && tries < 30 {
        data.BuyAttribute(stat);
        tries += 1;
      }
      index += 1;
    }
    data.AddDevelopmentPoints(300, gamedataDevelopmentPointType.Primary);
    data.AddDevelopmentPoints(50, gamedataDevelopmentPointType.Espionage);
    index = 0;
    while index < ArraySize(perkTypes) {
      let perk = IntEnum<gamedataNewPerkType>(perkTypes[index]);
      let tries = 0;
      if !data.IsNewPerkUnlocked(perk) {
        data.UnlockNewPerk(perk);
      }
      while data.IsNewPerkBought(perk) < perkLevels[index] && tries < 5 {
        data.BuyNewPerk(perk, true);
        tries += 1;
      }
      index += 1;
    }
    index = 0;
    while index < ArraySize(pointTypes) {
      let pointType = IntEnum<gamedataDevelopmentPointType>(pointTypes[index]);
      let missing = pointValues[index] - data.GetDevPoints(pointType);
      if missing != 0 {
        data.AddDevelopmentPoints(missing, pointType);
      }
      index += 1;
    }
    let right = 0;
    let wrong = 0;
    index = 0;
    while index < ArraySize(levelTypes) {
      if levelTypes[index] < EnumInt(gamedataProficiencyType.Count) && data.GetProficiencyLevel(IntEnum<gamedataProficiencyType>(levelTypes[index])) == levels[index] {
        right += 1;
      } else {
        wrong += 1;
      }
      index += 1;
    }
    index = 0;
    while index < ArraySize(attributeTypes) {
      if this.AttributeValue(IntEnum<gamedataStatType>(attributeTypes[index])) == attributeValues[index] {
        right += 1;
      } else {
        wrong += 1;
      }
      index += 1;
    }
    index = 0;
    while index < ArraySize(perkTypes) {
      if data.IsNewPerkBought(IntEnum<gamedataNewPerkType>(perkTypes[index])) == perkLevels[index] {
        right += 1;
      } else {
        wrong += 1;
      }
      index += 1;
    }
    index = 0;
    while index < ArraySize(pointTypes) {
      if data.GetDevPoints(IntEnum<gamedataDevelopmentPointType>(pointTypes[index])) == pointValues[index] {
        right += 1;
      } else {
        wrong += 1;
      }
      index += 1;
    }
    return IntToString(right) + " right " + IntToString(wrong) + " off (" + IntToString(ArraySize(levelTypes)) + " levels " +
      IntToString(ArraySize(attributeTypes)) + " attributes " + IntToString(ArraySize(perkTypes)) + " perks " + IntToString(ArraySize(pointTypes)) + " point kinds)";
  }
}

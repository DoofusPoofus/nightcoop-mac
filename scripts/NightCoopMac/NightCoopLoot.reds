// loot the script half of the plugins ItemGive and WorldLoot (nightcoops lootsync and worldloot)
// NightCoopGiveItem(record quantity peer) stands in for nightcoops native of that name: it queues the give in NightCoopItemGive
// the plugin takes the queue evry 100 ms checks it and runs the host ledger (worst case an item gets lost never duped).
// NightCoopInventoryStatus / NightCoopLootStatus hand back the plugins last status lines like the windows natives do
// NightCoopWorldLoot: a drop that comes into the world (gameItemDropObject.OnGameAttached) at most 3 s after our player dropped
// something (DropAction.CompleteAction) and at most 8 m from us is ours and lands in newOwnDrops for the plugin to share. a
// drop next to a spot the plugin watches (a mirror it spawned thru the partners body like nightcoop or one that streamed out)
// lands in boundTickets / boundDrops instead (within 3 m and one within 10 m of such a spot is never taken for ours). a spot
// stays watched until the plugin lets it go so a late second copy of a mirror gets caught too. the loot window
// (LootingGameController.OnUpdateData) says wat our player looks at
// Read fills the read fields with the items of a container TakeFrom / PutInto / RollBack move items around for the plugin
// quality goes out as nightcoops tier: Quality stat * 2 + IsItemPlus (0 Common .. 10 LegendaryPlusPlus 255 unknown)

public class NightCoopItemGive extends ScriptableService {
  private let m_askRecords: array<String>;
  private let m_askQuantities: array<Int32>;
  private let m_askPeers: array<Int32>;
  private let m_inventoryStatus: String;
  private let m_lootStatus: String;

  public static func Get() -> ref<NightCoopItemGive> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopItemGive") as NightCoopItemGive;
  }

  public func Ask(recordName: String, quantity: Int32, toPeer: Int32) -> String {
    ArrayPush(this.m_askRecords, recordName);
    ArrayPush(this.m_askQuantities, quantity);
    ArrayPush(this.m_askPeers, toPeer);
    return "inventory: " + IntToString(quantity) + " x '" + recordName + "' for peer " + IntToString(toPeer) + " goes to the plugin";
  }

  public func ClearAsks() {
    ArrayClear(this.m_askRecords);
    ArrayClear(this.m_askQuantities);
    ArrayClear(this.m_askPeers);
  }

  public func SetStatus(inventory: String, loot: String) {
    this.m_inventoryStatus = inventory;
    this.m_lootStatus = loot;
  }

  public func InventoryStatus() -> String {
    return this.m_inventoryStatus;
  }

  public func LootStatus() -> String {
    return this.m_lootStatus;
  }
}

public static func NightCoopGiveItem(recordName: String, quantity: Int32, toPeer: Int32) -> String {
  let give = NightCoopItemGive.Get();
  if !IsDefined(give) {
    return "inventory: no NightCoopItemGive service";
  }
  return give.Ask(recordName, quantity, toPeer);
}

public static func NightCoopInventoryStatus() -> String {
  let give = NightCoopItemGive.Get();
  return IsDefined(give) ? give.InventoryStatus() : "";
}

public static func NightCoopLootStatus() -> String {
  let give = NightCoopItemGive.Get();
  return IsDefined(give) ? give.LootStatus() : "";
}

public class NightCoopWorldLoot extends ScriptableService {
  private let m_sharing: Bool;
  private let m_ownDropAt: Float;
  private let m_newOwnDrops: array<EntityID>;
  private let m_watchTickets: array<Int32>;
  private let m_watchSpots: array<Vector4>;
  private let m_boundTickets: array<Int32>;
  private let m_boundDrops: array<EntityID>;
  private let m_lookedAt: EntityID;
  private let m_lookedAtActive: Bool;
  private let m_lookedAtSeen: Float;
  private let m_readItems: array<ItemID>;
  private let m_readQuantities: array<Int32>;
  private let m_readQualities: array<Int32>;
  private let m_readPosition: Vector4;
  private let m_readOrientation: Quaternion;
  private let m_readBag: Bool;
  private let m_mirrorItems: array<ItemID>;
  private let m_mirrorQuantities: array<Int32>;

  public static func Get() -> ref<NightCoopWorldLoot> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopWorldLoot") as NightCoopWorldLoot;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(GetGameInstance()));
  }

  private func ObjectOf(id: EntityID) -> ref<GameObject> {
    return GameInstance.FindEntityByID(GetGameInstance(), id) as GameObject;
  }

  public func Configure(sharing: Bool) {
    this.m_sharing = sharing;
  }

  public func ClearNews() {
    ArrayClear(this.m_newOwnDrops);
    ArrayClear(this.m_boundTickets);
    ArrayClear(this.m_boundDrops);
  }

  public func Forget() {
    this.ClearNews();
    ArrayClear(this.m_watchTickets);
    ArrayClear(this.m_watchSpots);
    this.m_ownDropAt = 0.0;
  }

  public func NoteOwnDrop() {
    this.m_ownDropAt = this.Now();
  }

  public func Watch(ticket: Int32, spot: Vector4) {
    this.Unwatch(ticket);
    ArrayPush(this.m_watchTickets, ticket);
    ArrayPush(this.m_watchSpots, spot);
  }

  public func Unwatch(ticket: Int32) {
    let index = 0;
    while index < ArraySize(this.m_watchTickets) {
      if this.m_watchTickets[index] == ticket {
        ArrayErase(this.m_watchTickets, index);
        ArrayErase(this.m_watchSpots, index);
        return;
      }
      index += 1;
    }
  }

  public func NoteDropAttached(drop: ref<gameItemDropObject>) {
    let spot = drop.GetWorldPosition();
    let nearest = -1;
    let nearestDistance = 10.0;
    let index = 0;
    while index < ArraySize(this.m_watchSpots) {
      let distance = Vector4.Distance(this.m_watchSpots[index], spot);
      if distance < nearestDistance {
        nearest = index;
        nearestDistance = distance;
      }
      index += 1;
    }
    if nearest >= 0 && nearestDistance < 3.0 {
      ArrayPush(this.m_boundTickets, this.m_watchTickets[nearest]);
      ArrayPush(this.m_boundDrops, drop.GetEntityID());
      return;
    }
    if nearest >= 0 || !this.m_sharing || this.m_ownDropAt <= 0.0 || this.Now() - this.m_ownDropAt > 3.0 {
      return;
    }
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) || Vector4.Distance(player.GetWorldPosition(), spot) > 8.0 {
      return;
    }
    ArrayPush(this.m_newOwnDrops, drop.GetEntityID());
  }

  public func NoteLooking(owner: EntityID, active: Bool) {
    this.m_lookedAt = owner;
    this.m_lookedAtActive = active;
    if active {
      this.m_lookedAtSeen = this.Now();
    }
  }

  public func LookedAtRecently(drop: EntityID) -> Bool {
    return Equals(this.m_lookedAt, drop) && (this.m_lookedAtActive || this.Now() - this.m_lookedAtSeen < 2.0);
  }

  private func QualityOf(itemData: wref<gameItemData>) -> Int32 {
    let quality = Cast<Int32>(itemData.GetStatValueByType(gamedataStatType.Quality));
    let plus = Cast<Int32>(itemData.GetStatValueByType(gamedataStatType.IsItemPlus));
    let tier = quality * 2 + plus;
    if tier < 0 || tier > 10 {
      return 255;
    }
    return tier;
  }

  public func Read(drop: EntityID) -> Bool {
    ArrayClear(this.m_readItems);
    ArrayClear(this.m_readQuantities);
    ArrayClear(this.m_readQualities);
    let object = this.ObjectOf(drop);
    if !IsDefined(object) {
      return false;
    }
    let list: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(object.GetGame()).GetItemList(object, list);
    let index = 0;
    while index < ArraySize(list) && ArraySize(this.m_readItems) < 64 {
      let itemData = list[index];
      if IsDefined(itemData) && itemData.GetQuantity() > 0 {
        ArrayPush(this.m_readItems, itemData.GetID());
        ArrayPush(this.m_readQuantities, itemData.GetQuantity());
        ArrayPush(this.m_readQualities, this.QualityOf(itemData));
      }
      index += 1;
    }
    this.m_readPosition = object.GetWorldPosition();
    this.m_readOrientation = object.GetWorldOrientation();
    let dropObject = object as gameItemDropObject;
    this.m_readBag = !IsDefined(dropObject) || !IsDefined(dropObject.GetItemObject());
    return true;
  }

  public func TakeFrom(drop: EntityID, item: ItemID, quantity: Int32) -> Bool {
    let object = this.ObjectOf(drop);
    if !IsDefined(object) || quantity <= 0 {
      return false;
    }
    let transactions = GameInstance.GetTransactionSystem(object.GetGame());
    if transactions.GetItemQuantity(object, item) < quantity {
      return false;
    }
    return transactions.RemoveItem(object, item, quantity);
  }

  public func PutInto(drop: EntityID, item: ItemID, quantity: Int32) -> Bool {
    let object = this.ObjectOf(drop);
    if !IsDefined(object) || quantity <= 0 {
      return false;
    }
    return GameInstance.GetTransactionSystem(object.GetGame()).GiveItem(object, item, quantity);
  }

  public func BeginMirror() {
    ArrayClear(this.m_mirrorItems);
    ArrayClear(this.m_mirrorQuantities);
  }

  public func AddMirrorItem(item: ItemID, quantity: Int32) {
    ArrayPush(this.m_mirrorItems, item);
    ArrayPush(this.m_mirrorQuantities, quantity);
  }

  public func SpawnMirror(body: ref<GameObject>, ticket: Int32, bag: Bool, position: Vector4, orientation: Quaternion) -> Bool {
    if !IsDefined(body) || ArraySize(this.m_mirrorItems) == 0 {
      return false;
    }
    let game = body.GetGame();
    let transactions = GameInstance.GetTransactionSystem(game);
    let instructions: array<DropInstruction>;
    let index = 0;
    while index < ArraySize(this.m_mirrorItems) {
      if transactions.GiveItem(body, this.m_mirrorItems[index], this.m_mirrorQuantities[index]) {
        ArrayPush(instructions, DropInstruction.Create(this.m_mirrorItems[index], this.m_mirrorQuantities[index]));
      }
      index += 1;
    }
    if ArraySize(instructions) == 0 {
      return false;
    }
    this.Watch(ticket, position);
    let lootManager = GameInstance.GetLootManager(game);
    if bag || ArraySize(instructions) > 1 {
      lootManager.SpawnItemDropOfManyItems(body, instructions, n"playerDropBag", position, orientation);
    } else {
      lootManager.SpawnItemDrop(body, this.m_mirrorItems[0], position, orientation);
    }
    return true;
  }

  public func RollBack(item: ItemID, quantity: Int32) -> Int32 {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) || quantity <= 0 {
      return 0;
    }
    let transactions = GameInstance.GetTransactionSystem(player.GetGame());
    let taking = Min(transactions.GetItemQuantity(player, item), quantity);
    if taking > 0 && transactions.RemoveItem(player, item, taking) {
      return taking;
    }
    return 0;
  }

  public func TestDrop(item: ItemID) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return false;
    }
    let game = player.GetGame();
    let transactions = GameInstance.GetTransactionSystem(game);
    if transactions.GetItemQuantity(player, item) < 1 {
      transactions.GiveItem(player, item, 1);
    }
    let spot = player.GetWorldPosition() + player.GetWorldForward() * 1.5;
    this.NoteOwnDrop();
    GameInstance.GetLootManager(game).SpawnItemDrop(player, item, spot, player.GetWorldOrientation());
    return true;
  }
}

@wrapMethod(DropAction)
public func CompleteAction(gameInstance: GameInstance) {
  let executor = this.GetExecutor();
  let player = GetPlayer(gameInstance);
  if IsDefined(executor) && IsDefined(player) && Equals(executor.GetEntityID(), player.GetEntityID()) {
    let loot = NightCoopWorldLoot.Get();
    if IsDefined(loot) {
      loot.NoteOwnDrop();
    }
  }
  wrappedMethod(gameInstance);
}

@wrapMethod(gameItemDropObject)
protected cb func OnGameAttached() -> Bool {
  let handled = wrappedMethod();
  let loot = NightCoopWorldLoot.Get();
  if IsDefined(loot) {
    loot.NoteDropAttached(this);
  }
  return handled;
}

@wrapMethod(LootingGameController)
protected cb func OnUpdateData(argValue: Variant) -> Bool {
  let loot = NightCoopWorldLoot.Get();
  if IsDefined(loot) {
    let data = FromVariant<LootData>(argValue);
    loot.NoteLooking(data.ownerId, data.isActive);
  }
  return wrappedMethod(argValue);
}

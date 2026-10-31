// body carry, the script half of nightcoops bodycarry (PlayerCarry 92, the plugin half is src/plugin/BodyCarry.cpp)
// FindCarried looks for an npc mounted on our player in leftShoulder (or bothHands for quest carries in the arms) and reads the
// body carrying values of our state machine blackboard, Describe reads record look and place of any entity for the message.
// on the partner side Attach mounts our body of the carried npc on the partner body in leftShoulder like the game does with a
// carried body and turns its ragdoll off, Pose writes the Carry and Mounting anim features on it, Detach takes it off with its
// ragdoll back on and Place puts it where the partner left it. HideForGood turns its visuals off after a partner put it away

public class NightCoopBodyCarry extends ScriptableService {
  private let m_carriedId: EntityID;
  private let m_carriedInArms: Bool;
  private let m_bodyCarrying: Int32;
  private let m_carryingDisposal: Bool;
  private let m_describedRecord: TweakDBID;
  private let m_describedAppearance: CName;
  private let m_describedPosition: Vector4;
  private let m_describedIsPuppet: Bool;

  public func FindCarried(allowArms: Bool) -> Bool {
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
      return false;
    }
    let board = player.GetPlayerStateMachineBlackboard();
    if IsDefined(board) {
      this.m_bodyCarrying = board.GetInt(GetAllBlackboardDefs().PlayerStateMachine.BodyCarrying);
      this.m_carryingDisposal = board.GetBool(GetAllBlackboardDefs().PlayerStateMachine.CarryingDisposal);
    }
    let playerId = player.GetEntityID();
    let noChild: EntityID;
    let anySlot: MountingSlotId;
    let mountings = GameInstance.GetMountingFacility(GetGameInstance()).GetMountingInfoMultipleWithIds(noChild, playerId, anySlot);
    for mounting in mountings {
      if Equals(mounting.parentId, playerId) && EntityID.IsDefined(mounting.childId) {
        let inArms = Equals(mounting.slotId.id, n"bothHands");
        if Equals(mounting.slotId.id, n"leftShoulder") || (allowArms && inArms) {
          this.m_carriedId = mounting.childId;
          this.m_carriedInArms = inArms;
          return true;
        }
      }
    }
    return false;
  }

  public func Describe(id: EntityID) -> Bool {
    let entity = GameInstance.FindEntityByID(GetGameInstance(), id);
    if !IsDefined(entity) {
      return false;
    }
    this.m_describedPosition = entity.GetWorldPosition();
    this.m_describedAppearance = entity.GetCurrentAppearanceName();
    let puppet = entity as ScriptedPuppet;
    this.m_describedIsPuppet = IsDefined(puppet);
    if IsDefined(puppet) {
      this.m_describedRecord = puppet.GetRecordID();
    } else {
      this.m_describedRecord = TDBID.None();
    }
    return true;
  }

  public func Attach(body: ref<GameObject>, npcId: EntityID) -> Bool {
    let npc = GameInstance.FindEntityByID(GetGameInstance(), npcId) as GameObject;
    if !IsDefined(body) || !IsDefined(npc) {
      return false;
    }
    let shoulder: MountingSlotId;
    shoulder.id = n"leftShoulder";
    let mounting: MountingInfo;
    mounting.childId = npcId;
    mounting.parentId = body.GetEntityID();
    mounting.slotId = shoulder;
    let data = new MountEventData();
    data.slotName = n"leftShoulder";
    data.mountParentEntityId = body.GetEntityID();
    data.isInstant = true;
    data.isCarrying = true;
    let request = new MountingRequest();
    request.lowLevelMountingInfo = mounting;
    request.preservePositionAfterMounting = false;
    request.mountData = data;
    GameInstance.GetMountingFacility(GetGameInstance()).Mount(request);
    this.DisableRagdoll(npc, true);
    return true;
  }

  public func Pose(npcId: EntityID, state: Int32, pickupAnimation: Int32, flags: Int32) {
    let npc = GameInstance.FindEntityByID(GetGameInstance(), npcId) as GameObject;
    if !IsDefined(npc) {
      return;
    }
    let carry = new AnimFeature_Carry();
    carry.state = state;
    carry.pickupAnimation = pickupAnimation;
    carry.useBothHands = (flags & 1) != 0;
    carry.instant = (flags & 2) != 0;
    carry.fastMode = (flags & 4) != 0;
    carry.isCarryActive = (flags & 8) != 0;
    carry.isFriendlyCarry = (flags & 16) != 0;
    carry.wasThrown = (flags & 32) != 0;
    AnimationControllerComponent.ApplyFeatureToReplicate(npc, n"Carry", carry, 0.0);
    let mounting = new AnimFeature_Mounting();
    mounting.mountingState = (flags & 8) != 0 ? 2 : 0;
    AnimationControllerComponent.ApplyFeatureToReplicate(npc, n"Mounting", mounting, 0.0);
  }

  public func Detach(npcId: EntityID) {
    let facility = GameInstance.GetMountingFacility(GetGameInstance());
    let noParent: EntityID;
    let anySlot: MountingSlotId;
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = facility.GetMountingInfoSingleWithIds(npcId, noParent, anySlot);
    facility.Unmount(request);
    let npc = GameInstance.FindEntityByID(GetGameInstance(), npcId) as GameObject;
    if IsDefined(npc) {
      this.Pose(npcId, 0, 0, 0);
      this.DisableRagdoll(npc, false);
    }
  }

  public func Place(npcId: EntityID, position: Vector4) {
    let npc = GameInstance.FindEntityByID(GetGameInstance(), npcId) as GameObject;
    if !IsDefined(npc) {
      return;
    }
    let facing: EulerAngles;
    facing.Yaw = npc.GetWorldYaw();
    GameInstance.GetTeleportationFacility(GetGameInstance()).Teleport(npc, position, facing);
    npc.QueueEvent(CreateForceRagdollNoPowerPoseEvent(n"NightCoopBodyCarry"));
  }

  public func HideForGood(npcId: EntityID) {
    let npc = GameInstance.FindEntityByID(GetGameInstance(), npcId);
    if !IsDefined(npc) {
      return;
    }
    for component in npc.GetComponents() {
      if IsDefined(component as IVisualComponent) {
        component.Toggle(false);
      }
    }
  }

  private func DisableRagdoll(npc: ref<GameObject>, disable: Bool) {
    let puppet = npc as NPCPuppet;
    if IsDefined(puppet) {
      puppet.SetDisableRagdoll(disable, true, false);
    }
  }
}

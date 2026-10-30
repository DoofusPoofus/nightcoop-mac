// the script side of nightcoops real partner body (session.txt 'body player'), rebuilt from its script patch table
// a partner body is a second PlayerPuppet that codeware spawned with the NightCoopPartnerBody tag. for those only:
//   components: wat ScriptedPuppet requests plus the aim point and environment damage receiver, no phone, quick slots,
//     player or vehicle cameras (the mandatory vehicle ones are kept off the body by the native builder)
//   attach: ScriptedPuppets attach, then the players sense type and visibility offsets. no PlayerAttachRequest, input,
//     facts, ui blackboards, stat pools, grace period or radio of a local player
//   detach: ScriptedPuppets detach, no PlayerDetachRequest
//   status effects, being noticed, laser sights and car hits do nothing, death / item slots go to ScriptedPuppet,
//   kills, inventory, item appearance and encumbrance callbacks of the local player are skipped, the corpse is not lootable
// the plugin looks for the NightCoopPartnerBodies class at runtime and only spawns PlayerPuppet bodies when its there.
// needs codeware for its DynamicEntitySystem tags and redscript for mac (engine/tools/scc) to compile

public abstract class NightCoopPartnerBodies {
  public static func Is(object: ref<GameObject>) -> Bool {
    if !IsDefined(object) {
      return false;
    }
    let dynamicEntities = GameInstance.GetDynamicEntitySystem();
    return IsDefined(dynamicEntities) && dynamicEntities.IsTagged(object.GetEntityID(), n"NightCoopPartnerBody");
  }
}

@wrapMethod(PlayerPuppet)
protected cb func OnRequestComponents(ri: EntityRequestComponentsInterface) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnRequestComponents(ri);
    EntityRequestComponentsInterface.RequestComponent(ri, n"targeting_primary", n"gameTargetingComponent", false);
    EntityRequestComponentsInterface.RequestComponent(ri, n"environmentDamageReceiver", n"EnvironmentDamageReceiverComponent", false);
    return true;
  }
  return wrappedMethod(ri);
}

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnGameAttached();
    this.UpdateSecondaryVisibilityOffset(false);
    this.EnableCombatVisibilityDistances(false);
    this.SetSenseObjectType(gamedataSenseObjectType.Player);
    return true;
  }
  return wrappedMethod();
}

@wrapMethod(PlayerPuppet)
protected cb func OnDetach() -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnDetach();
    return true;
  }
  return wrappedMethod();
}

@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectRemoved(evt: ref<RemoveStatusEffect>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnTargetNeutraliziedEvent(evt: ref<TargetNeutraliziedEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnDeath(evt);
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(ScriptedPuppet)
protected func UpdateLootInteraction() {
  if NightCoopPartnerBodies.Is(this) {
    return;
  }
  wrappedMethod();
}

@wrapMethod(PlayerPuppet)
protected cb func OnBeingNoticed(evt: ref<OnBeingNoticed>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnClearBeingNoticedBB(evt: ref<ClearBeingNoticedBB>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnBeingTargetByLaserSight(evt: ref<BeingTargetByLaserSightUpdateEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemAddedToSlot(evt: ref<ItemAddedToSlot>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnItemAddedToSlot(evt);
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemRemovedFromSlot(evt: ref<ItemRemovedFromSlot>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    super.OnItemRemovedFromSlot(evt);
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnClearItemAppearanceEvent(evt: ref<ClearItemAppearanceEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnResetItemAppearanceEvent(evt: ref<ResetItemAppearanceEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnUnderwearEquipFailsafeEvent(evt: ref<UnderwearEquipFailsafeEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemAddedToInventory(evt: ref<ItemAddedEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemChangedEvent(evt: ref<ItemChangedEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemBeingRemovedFromInventory(evt: ref<ItemBeingRemovedEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnInventoryEmpty(evt: ref<OnInventoryEmptyEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnEvaluateEncumbranceEvent(evt: ref<EvaluateEncumbranceEvent>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnCarHitPlayer(evt: ref<OnCarHitPlayer>) -> Bool {
  if NightCoopPartnerBodies.Is(this) {
    return false;
  }
  return wrappedMethod(evt);
}

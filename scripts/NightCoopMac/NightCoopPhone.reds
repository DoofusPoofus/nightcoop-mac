// phone the script half of the plugins PhoneShare module (nightcoops phonesync PhoneSync 133)
// mode 1 (host with partners in its world): evry call phase its PhoneSystem gets (OnTriggerCall) is noted for the plugin to commit
// to evryone. mode 2 (guest in the hosts world): the guests own quest calls are swallowed (the hosts quest makes them) and a call
// our player dials itself is noted as a request to the host instead. Play queues a call phase on our PhoneSystem like the game
// does (QueueRequest) and lets it thru the wrap once (bypass count). a guest answering or rejecting a ringing call (SetPhoneFact
// talking / rejected) is noted as a pickup (action 2) for the host and Pickup answers or rejects the call on the hosts phone

public class NightCoopPhoneCall {
  public let contact: CName;
  public let callPhase: Int32;
  public let callMode: Int32;
  public let visuals: Int32;
  public let flags: Int32;
  public let action: Int32;
}

public class NightCoopPhone extends ScriptableService {
  private let m_mode: Int32;
  private let m_bypass: Int32;
  private let m_outgoing: array<ref<NightCoopPhoneCall>>;
  private let m_noted: Int32;
  private let m_swallowed: Int32;
  private let m_played: Int32;

  public static func Get() -> ref<NightCoopPhone> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopPhone") as NightCoopPhone;
  }

  public func Configure(mode: Int32) {
    this.m_mode = mode;
  }

  public func LetsThru(request: ref<questTriggerCallRequest>) -> Bool {
    if this.m_bypass > 0 {
      this.m_bypass -= 1;
      return true;
    }
    if this.m_mode == 0 || !IsDefined(request) {
      return true;
    }
    let playerCalling = Equals(request.caller, n"Player");
    let call = new NightCoopPhoneCall();
    call.contact = playerCalling ? request.addressee : request.caller;
    call.callPhase = EnumInt(request.callPhase);
    call.callMode = EnumInt(request.callMode);
    call.visuals = EnumInt(request.visuals);
    call.flags = (playerCalling ? 1 : 0) + (request.isPlayerTriggered ? 2 : 0) + (request.isRejectable ? 4 : 0) + (request.showAvatar ? 8 : 0);
    call.action = 1;
    if this.m_mode == 1 {
      ArrayPush(this.m_outgoing, call);
      this.m_noted += 1;
      return true;
    }
    if request.isPlayerTriggered {
      ArrayPush(this.m_outgoing, call);
      this.m_noted += 1;
    }
    this.m_swallowed += 1;
    return false;
  }

  public func NotePickup(isPlayerCalling: Bool, contact: CName, state: questPhoneTalkingState) {
    if this.m_mode != 2 || this.m_bypass > 0 || (NotEquals(state, questPhoneTalkingState.Talking) && NotEquals(state, questPhoneTalkingState.Rejected)) {
      return;
    }
    let call = new NightCoopPhoneCall();
    call.contact = contact;
    call.callPhase = Equals(state, questPhoneTalkingState.Talking) ? 2 : 3;
    call.flags = (isPlayerCalling ? 1 : 0) + (Equals(state, questPhoneTalkingState.Rejected) ? 32 : 0);
    call.action = 2;
    ArrayPush(this.m_outgoing, call);
    this.m_noted += 1;
  }

  public func Pickup(reject: Bool) -> Bool {
    let phone = GameInstance.GetScriptableSystemsContainer(GetGameInstance()).Get(n"PhoneSystem") as PhoneSystem;
    if !IsDefined(phone) {
      return false;
    }
    let request = new PickupPhoneRequest();
    request.CallInformation = phone.NightCoopLastCall();
    request.shouldBeRejected = reject;
    this.m_bypass += 1;
    phone.QueueRequest(request);
    this.m_played += 1;
    return true;
  }

  public func TakeOutgoing() -> array<ref<NightCoopPhoneCall>> {
    let taken = this.m_outgoing;
    ArrayClear(this.m_outgoing);
    return taken;
  }

  public func Play(contact: CName, callPhase: Int32, callMode: Int32, visuals: Int32, flags: Int32) -> Bool {
    let phone = GameInstance.GetScriptableSystemsContainer(GetGameInstance()).Get(n"PhoneSystem") as PhoneSystem;
    if !IsDefined(phone) {
      return false;
    }
    let playerCalling = (flags & 1) != 0;
    let request = new questTriggerCallRequest();
    request.caller = playerCalling ? n"Player" : contact;
    request.addressee = playerCalling ? contact : n"Player";
    request.callPhase = IntEnum<questPhoneCallPhase>(callPhase);
    request.callMode = IntEnum<questPhoneCallMode>(callMode);
    request.visuals = IntEnum<questPhoneCallVisuals>(visuals);
    request.isPlayerTriggered = (flags & 2) != 0;
    request.isRejectable = (flags & 4) != 0;
    request.showAvatar = (flags & 8) != 0;
    this.m_bypass += 1;
    phone.QueueRequest(request);
    this.m_played += 1;
    return true;
  }

  public func Describe() -> String {
    return "mode " + IntToString(this.m_mode) + ", noted " + IntToString(this.m_noted) + ", swallowed " + IntToString(this.m_swallowed) + ", played " +
      IntToString(this.m_played);
  }
}

@wrapMethod(PhoneSystem)
private final func OnTriggerCall(request: ref<questTriggerCallRequest>) {
  let phone = NightCoopPhone.Get();
  if IsDefined(phone) && !phone.LetsThru(request) {
    return;
  }
  wrappedMethod(request);
}

@wrapMethod(PhoneSystem)
private final func SetPhoneFact(isPlayerCalling: Bool, contactName: CName, state: questPhoneTalkingState) {
  let phone = NightCoopPhone.Get();
  if IsDefined(phone) {
    phone.NotePickup(isPlayerCalling, contactName, state);
  }
  wrappedMethod(isPlayerCalling, contactName, state);
}

@addMethod(PhoneSystem)
public func NightCoopLastCall() -> PhoneCallInformation {
  return this.m_LastCallInformation;
}

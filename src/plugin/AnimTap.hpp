/*
 anim tap, the mac take on nightcoops AnimTap: records the inputs the game writes into our players anim graph so partners can
 replay them on our body (their AnimMirror, or ours on a mac partner)
 nightcoop hooks the anim controller components input writes by address hash, none of them are in the mac db. on mac 2.5.0 they
 were found by following the script natives (SetInputFloat & co, registered at game+0x18b45xx) to the component level setters
 they call, which the games own c++ calls too:
   PushEvent game+0x18b5dfc (comp, name) | SetInputFloat +0x18b60d8 (comp, name, float) | SetInputInt +0x18b6270 (comp, name, int)
   SetInputBool +0x18b6408 (comp, name, bool) | SetInputVector +0x18b633c (comp, name, Vector4*)
   SetInputQuaternion +0x18b61a4 (comp, name, Quaternion*) | ApplyFeature +0x18b64d4 (comp, name, Handle<animAnimFeature>*)
 the player state machine writes thru the script statics (AnimationControllerComponent.ApplyFeature / SetInputFloat / PushEvent
 ...), which queue an entAnimInputSetter* / entAnimExternalEvent on the entity instead, so the entity event queue push is hooked too
 (game+0x1021abc (entity +0xd8 queue, Handle<Event>*), the native QueueEvent calls it): events queued on our player get their key
 and value (or name) read thru rtti. each is hooked only when its first 24 code bytes are the ones seen in 2.5.0. a write is kept only when the component is our
 players anim controller (looked up from the main thread evry 100 ms, so a new players setup writes get caught), features keep evry rtti field up to 32 bytes like nightcoops batches
 a value write that repeats the last queued value of that input is not queued again (the partners graph still holds it),
 events always go. the main thread sends the queued writes as PlayerAnim (29) batches on slot 0 evry 50 ms, and a partners PlayerAnimWant gets a
 full state batch (the last value of evry input seen) sent to them alone. wrapper weights and graph attach / detach have no
 component setter here and are not recorded
*/
#pragma once

#include "net/GameMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::anim_tap
{
using LogFunction = std::function<void(const std::string&)>;

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log);
void Tick(uint64_t nowMs, net::RelaySession& session);
void SendFullState(uint32_t peerNumber, net::RelaySession& session);
void Forget();
}

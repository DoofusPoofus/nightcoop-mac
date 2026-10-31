/*
 device sync a first cut of nightcoops devicesync turret_host_led and barrels (DeviceSync 139 TurretWord 214 DeviceBlast
 130; the script half is NightCoopDeviceSync in scripts/NightCoopMac/NightCoopDevices.reds). it runs while a world is shared:
 the host with partners in its world a guest in the hosts world (or evrywhere with share_devices in session.txt)
 our side: evry 100 ms the script queues get read. a device action our player finished gets copied thru rtti (its class name
 evry scalar field up to gamedeviceAction duration quickhack flag and its prop) and sorted by its class name like the dll
 does (not shared state only the same action the same action at the host only a skill check). 150 ms later the state
 after gets read: a guest sends kind 1 to the host the host sends kind 2 (the same action skill checks deaths) or kind 3
 (state changes) to evry partner in its world. devices our player destroyed go out as action kind 4
 the host takes a guests kind 1 like the game would: refused when its copy is disabled or without power else it does the
 same action (built from the class name and fields SetUp + SetExecutor(our player) + ExecutePSAction) marks the skill check
 destroys the device or forces the state with the quest actions. it answers the asker with kind 3 and tells evryone else
 a guest follows kind 2 / 3 from the host by version (older ones are dropped) the same way and checks a forced state up to
 3 times. a device whose ps is not here yet waits 30 s a device to destroy that is not streamed in gets disabled and
 destroyed once it shows up
 turrets: a guests turret that wants our player asks the host at most once a second (TurretWord 1) and lets our player go
 while the hosts same turret said calm in the last 3 s (its targets get re-evaluated when that flips). the host answers with
 its turrets followed target
 barrels: an explosive device that starts to blow here goes to evryone as a live blast a partners blast blows our copy
 without damage when their player blew it (with damage when it went off by itself there). late partners in the hosts world
 get a snapshot of the devices that blew those are set exploded and hidden quietly
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::device_sync
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool shareAlways, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
}

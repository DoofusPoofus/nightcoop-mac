/*
 state machine board: reads ints of our players state machine blackboard (GetPlayerStateMachineBlackboard with the ids from
 GetAllBlackboardDefs().PlayerStateMachine), like nightcoops player bridge does for Vitals, Vehicle, TimeDilation and the rest.
 the property of each name is looked up once. main thread only
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>

namespace nightcoop_mac::state_machine_board
{
bool ReadInt(RED4ext::Handle<RED4ext::IScriptable>& player, const char* name, int32_t& value);
}

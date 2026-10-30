/*
 small helpers for calling game functions thru rtti from the main thread
 statics get called with no context and methods on an instance we already hold a game made handle for
 FindFunction takes the rtti name and falls back to the short name, scripted ones are listed as Name;Params
 never wrap a raw game pointer in a new Handle here: on mac that starts a second refcount and the game
 object dies when ours does (thats what crashed the first player sampler)
 ParamsAre checks the type names of a functions params against what the port expects so a changed
 signature turns a feature off instead of crashing
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <initializer_list>
#include <string>

namespace nightcoop_mac::game
{
RED4ext::CBaseFunction* FindFunction(const char* className, const char* functionName);
bool ParamsAre(RED4ext::CBaseFunction* function, std::initializer_list<const char*> typeNames);
bool ReturnIs(RED4ext::CBaseFunction* function, const char* typeName);
bool CallStatic(RED4ext::CBaseFunction* function, void* out, RED4ext::StackArgs_t& args);
bool CallMethod(void* instance, RED4ext::CBaseFunction* function, void* out, RED4ext::StackArgs_t& args);
std::string Describe(RED4ext::CBaseFunction* function);
}

/*
 rtti dump for porting work
 if rtti-dump-request.txt sits next to the plugin, the classes and global functions it names get written to
 rtti-dump.txt once the game is running (the main menu is enough). lines look like
   class DynamicEntitySystem
   enum moveMovementType
   function GetPlayer;GameInstance
   vtable GetCharacterCustomizationSystem   (a ScriptGameInstance getter: its system objects vtable slots as game offsets)
   peek 7da1500                              (4 words at game+offset, for statics that only get filled at runtime)
 the dump runs again 30 s into the Running state so game systems exist by then. only the vtable line calls anything (the getter)
*/
#pragma once

#include <filesystem>

namespace nightcoop_mac
{
bool WriteRttiDumpIfRequested(const std::filesystem::path& pluginFolder);
}

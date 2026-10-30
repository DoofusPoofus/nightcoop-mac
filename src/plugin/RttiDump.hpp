/*
 rtti dump for porting work
 if rtti-dump-request.txt sits next to the plugin, the classes and global functions it names get written to
 rtti-dump.txt once the game is running (the main menu is enough). lines look like
   class DynamicEntitySystem
   enum moveMovementType
   function GetPlayer;GameInstance
 only reads the games type info, never calls anything
*/
#pragma once

#include <filesystem>

namespace nightcoop_mac
{
bool WriteRttiDumpIfRequested(const std::filesystem::path& pluginFolder);
}

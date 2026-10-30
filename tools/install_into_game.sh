#!/bin/sh
# copies the built NightCoopMac.dylib into the games red4ext plugins folder and signs it ad hoc
# scripts/NightCoopMac goes to the plugins Scripts folder, the red4ext launcher compiles it when redscript (engine/tools/scc) is there
# RED4ext-macos has to be installed in the game already (see the readme)
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-"$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"}
plugin="$here/build/NightCoopMac.dylib"

if [ ! -f "$game/red4ext/RED4ext.dylib" ]; then
    echo "no RED4ext in $game, install RED4ext-macos first"
    exit 1
fi
if [ ! -f "$plugin" ]; then
    echo "no $plugin yet, build it first (cmake --build build)"
    exit 1
fi

mkdir -p "$game/red4ext/plugins/NightCoopMac"
install -m 755 "$plugin" "$game/red4ext/plugins/NightCoopMac/NightCoopMac.dylib"
codesign -f -s - "$game/red4ext/plugins/NightCoopMac/NightCoopMac.dylib"
rm -rf "$game/red4ext/plugins/NightCoopMac/Scripts"
cp -R "$here/scripts/NightCoopMac" "$game/red4ext/plugins/NightCoopMac/Scripts"
echo "installed, start the game with \"$game/launch_red4ext.sh\""

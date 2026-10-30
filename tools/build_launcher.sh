#!/bin/sh
# builds the little launcher window into build/NightCoop Mac.app
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
app="$here/build/NightCoop Mac.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
swiftc -O -parse-as-library -target arm64-apple-macos14 "$here/launcher/NightCoopLauncher.swift" -o "$app/Contents/MacOS/NightCoopMacLauncher"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>NightCoopMacLauncher</string>
    <key>CFBundleIdentifier</key>
    <string>com.doofuspoofus.nightcoop-mac-launcher</string>
    <key>CFBundleName</key>
    <string>NightCoop Mac</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.0.2</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST
codesign -f -s - "$app"
echo "built $app"

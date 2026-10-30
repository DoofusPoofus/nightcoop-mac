#!/bin/sh
# runs ghidra headless over NightCoop.dll and drops the c output in decomp/
# usage: tools/decompile.sh [path to NightCoop.dll]

here=$(cd "$(dirname "$0")/.." && pwd)
dll=${1:-$here/binaries/red4ext/plugins/NightCoop/NightCoop.dll}
ghidra_home=${GHIDRA_HOME:-/opt/homebrew/Cellar/ghidra/12.2/libexec}
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home}

mkdir -p "$here/ghidra-project" "$here/decomp"
"$ghidra_home/support/analyzeHeadless" "$here/ghidra-project" nightcoop \
    -import "$dll" -overwrite \
    -scriptPath "$here/tools" \
    -postScript DumpEverything.java "$here/decomp" \
    -max-cpu 8

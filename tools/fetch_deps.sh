#!/bin/sh
# grabs evrything nightcoop-mac builds against into third_party/ at the commits it was tested with
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
third_party="$here/third_party"
mkdir -p "$third_party"

fetch() {
    name=$1
    url=$2
    commit=$3
    if [ ! -d "$third_party/$name/.git" ]; then
        git clone --quiet "$url" "$third_party/$name"
    fi
    git -C "$third_party/$name" fetch --quiet origin
    git -C "$third_party/$name" checkout --quiet --detach "$commit"
    git -C "$third_party/$name" submodule update --quiet --init --recursive
    echo "$name at $(git -C "$third_party/$name" rev-parse --short HEAD)"
}

fetch RED4ext-macos https://github.com/codeman101/RED4ext-macos.git d332b54
fetch nightcoop-hub https://github.com/Twenfys/nightcoop-hub.git 6b694a6
fetch enet https://github.com/lsalzman/enet.git 5a9c537fd464b3c6d3c55e1d3bd47588faf71b42

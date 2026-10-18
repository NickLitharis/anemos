#!/bin/bash
# Run the tree's own razzle build.exe under Wine on one directory.
# usage: harness/razzle.sh 'base\ntos\rtl\up' [-cZ]
# Output: the directory's build.log / build.err; binaries in Source/XPSP1/binaries.x86fre
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
ln -sfn "$ROOT/Source/XPSP1" "$WINEPREFIX/dosdevices/w:"
mkdir -p "$ROOT/Source/XPSP1/binaries.x86fre"
dir=$1; shift
wine cmd /c "$(winepath -w "$ROOT/harness/razzle-env.cmd")" "$dir" "${@:--cZ}" 2>&1 | tr -d '\r'

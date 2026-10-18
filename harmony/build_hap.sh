#!/usr/bin/env bash
# Builds the HarmonyOS companion application as an UNSIGNED package and puts it next to the program,
# where qt-scrcpy-gui picks it up, signs it for the connected device and installs it.
#
# Needs DevEco Studio (for hvigor and the SDK), or hvigorw on $PATH.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../qtscrcpy-harmony-unsigned.hap"

if command -v hvigorw >/dev/null 2>&1; then
    hvigor=hvigorw
elif [ -x "$here/hvigorw" ]; then
    hvigor="$here/hvigorw"
elif [ -x "/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw" ]; then
    hvigor="/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw"
else
    echo "Could not find hvigorw. Install DevEco Studio, or open this project in it and build" >&2
    echo '"Build > Build Hap(s)" without a signing configuration.' >&2
    exit 1
fi

cd "$here"
if command -v ohpm >/dev/null 2>&1; then
    ohpm install --all
elif [ -x "/Applications/DevEco-Studio.app/Contents/tools/ohpm/bin/ohpm" ]; then
    "/Applications/DevEco-Studio.app/Contents/tools/ohpm/bin/ohpm" install --all
fi

"$hvigor" --mode module -p product=default -p buildMode=debug assembleHap --no-daemon --no-parse-dependency

built="$(find "$here/entry/build" -name '*-unsigned.hap' -o -name '*.hap' 2>/dev/null | head -1)"
if [ -z "$built" ]; then
    echo "No .hap was produced under entry/build." >&2
    exit 1
fi
cp -f "$built" "$out"
echo
echo "Copied to: $out"
echo 'Now start qt-scrcpy-gui, connect the phone and press "start mirroring": the package is signed'
echo "for that phone and installed automatically."

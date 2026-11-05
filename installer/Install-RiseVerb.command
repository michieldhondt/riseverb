#!/bin/bash
# Installs RiseVerb (AU + VST3) for the current user and clears the macOS download quarantine.
cd "$(dirname "$0")"
COMP=~/Library/Audio/Plug-Ins/Components
VST3=~/Library/Audio/Plug-Ins/VST3
mkdir -p "$COMP" "$VST3"
rm -rf "$COMP/RiseVerb.component" "$VST3/RiseVerb.vst3"
cp -R RiseVerb.component "$COMP/"
cp -R RiseVerb.vst3 "$VST3/"
xattr -dr com.apple.quarantine "$COMP/RiseVerb.component" "$VST3/RiseVerb.vst3" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "RiseVerb is installed."
echo "Restart Logic Pro. You'll find it under: Audio FX > Audio Units > M1lobeats > RiseVerb"
echo ""
read -n 1 -s -r -p "Press any key to close..."

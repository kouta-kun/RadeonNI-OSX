#!/bin/sh
# run both saves at several quality settings, restoring the user's config afterwards
C=~/Library/Application\ Support/Doom\ 3\ Demo/demo/DoomConfig.cfg
cp -p "$C" ~/DoomConfig.user-backup.cfg
ULTRA="+set image_useCompression 0 +set image_useNormalCompression 0 +set image_usePrecompressedTextures 0 +set image_anisotropy 8 +set image_lodbias 0 +set image_downSize 0"
run() { name=$1; args=$2; for s in bench bench2; do printf "%s, %s: " "$name" "$s"; D3ARGS="$args" ~/gl/d3save.sh $s; tr "\r" "\n" < ~/Library/Application\ Support/Doom\ 3\ Demo/demo/qconsole.log | grep -i -E "multisample|anisotrop|couldn.t|error|fail" | grep -v -i "guisounds\|autoexec\|Couldn.t load sound" | head -3; cp -p ~/DoomConfig.user-backup.cfg "$C"; done; }
run "as configured" ""
run "ultra quality" "$ULTRA"
run "ultra + 4x antialiasing" "$ULTRA +set r_multiSamples 4"
cp -p ~/DoomConfig.user-backup.cfg "$C"

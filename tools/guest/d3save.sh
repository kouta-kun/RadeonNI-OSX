#!/bin/sh
# Doom 3 demo benchmark from a save game: load it, let it settle for 60
# frames, then time 300 frames between two marks in the console log.
#
#   d3save.sh <save name> [environment assignments for the game]
#
# D3ARGS in the environment of this script is put on the game's command
# line before the save is loaded (settings: "+set r_multiSamples 4").
# The game writes changed settings to its config when it quits: keep a
# copy of DoomConfig.cfg if they are not meant to stay.
#
# The save is made in the game (console: saveGame <name>) at a place worth
# measuring. Works with any graphics card: nothing here is ours.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
save=$1; shift
D=~/Library/Application\ Support/Doom\ 3\ Demo/demo
L="$D/qconsole.log"
printf 'wait 60\necho MARK1\nwait 300\necho MARK2\nwait 5\nquit\n' > "$D/d3save.cfg"
rm -f "$L"
cd ~/Desktop/Doom\ 3\ Demo
env A=1 "$@" ./Doom\ 3\ Demo.app/Contents/MacOS/Doom\ 3\ Demo +set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080 +set r_fullscreen 1 +set r_aspectRatio 1 +set logFile 2 $D3ARGS +loadGame "$save" +exec d3save.cfg > /tmp/d3.out 2>&1 &
perl -MTime::HiRes=time,sleep -e '
  my ($log, $frames) = @ARGV; my ($t0, $t1); my $deadline = time + 300;
  while (time < $deadline) {
    if (open(my $f, "<", $log)) { local $/; my $s = <$f>; close($f);
      $t0 = time if !$t0 && $s =~ /MARK1/;
      if ($t0 && $s =~ /MARK2/) { $t1 = time; last; } }
    sleep(0.02);
  }
  if ($t1) { printf("%d frames, %.1f seconds: %.1f fps\n", $frames, $t1 - $t0, $frames / ($t1 - $t0)); }
  else { print "the run did not finish\n"; }' "$L" 300
sleep 4
killall "Doom 3 Demo" 2>/dev/null

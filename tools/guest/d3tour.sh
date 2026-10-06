#!/bin/sh
# Doom 3 demo benchmark: a scripted tour of demo_mc_underground (tour.cfg),
# 200 frames between two marks in the console log; prints frames a second.
# Arguments are environment assignments for the game.
L=~/Library/Application\ Support/Doom\ 3\ Demo/demo/qconsole.log
rm -f "$L"
cd ~/Desktop/Doom\ 3\ Demo
env "$@" ./Doom\ 3\ Demo.app/Contents/MacOS/Doom\ 3\ Demo +set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080 +set r_fullscreen 1 +set r_aspectRatio 1 +set logFile 2 +devmap game/demo_mc_underground +exec tour.cfg > /tmp/d3.out 2>&1 &
perl -MTime::HiRes=time,sleep -e '
  my ($log, $frames) = @ARGV; my ($t0, $t1); my $deadline = time + 240;
  while (time < $deadline) {
    if (open(my $f, "<", $log)) { local $/; my $s = <$f>; close($f);
      $t0 = time if !$t0 && $s =~ /TOURSTART/;
      if ($t0 && $s =~ /TOUREND/) { $t1 = time; last; } }
    sleep(0.05);
  }
  if ($t1) { printf("%d frames, %.1f seconds: %.1f fps\n", $frames, $t1 - $t0, $frames / ($t1 - $t0)); }
  else { print "the tour did not finish\n"; }' "$L" 200
sleep 4
killall "Doom 3 Demo" 2>/dev/null
grep -c "out of video memory" /tmp/d3.out

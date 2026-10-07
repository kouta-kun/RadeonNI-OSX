#!/bin/sh
# What moving windows costs the window server, for comparing one set-up
# with another (Quartz Extreme on and off, a change to the driver).
#
#   sudo qebench.sh <label> <output directory>
#
# Run on the Mac, logged in at the console, from a shell of the user who is
# (ssh will do). Needs root because sample(1) may only look into the
# window server as root; what touches the screen runs as the user sudo was
# called by. Uses ~/gl/drag, ~/gl/fences and ~/gl/qe (tools/guest/) where
# they exist; without the accelerator the GPU column is "n/a".
#
# Four phases of about ten seconds each:
#   move    a Finder window, opened here at a known place, is moved in
#           steps by AppleScript as fast as the Finder takes them; the
#           number of moves completed is the result
#   drag    the same window dragged by its title bar with ~/gl/drag, back
#           and forth; the pointer's pace is fixed, so the cost is the result
#   expose  Expose in and out (F9 through System Events)
#   idle    nothing, for a baseline
# and for each: wall time, the window server's CPU time, the command
# buffers the GPU completed (fence counter before and after), calls of the
# 2D plug-in's blitters if it is counting (touch /tmp/rdnga.on before the
# window server starts; ga/RadeonNIGA.c), and a sample(1) profile of the
# window server (scripts/sample-profile.py on the host reads it). The
# plug-in writes its counts when it is called, at most once a second: a
# phase's last calls can show in the next phase's row.
#
# Prints one table and leaves in the output directory, all named
# <label>.*: summary.txt (the table), raw.txt (the numbers behind it),
# <phase>.sample.txt, <phase>.surface.txt (the kext's surface call counts
# from the registry, after the phase; start.surface.txt before the first),
# <phase>.ga.txt (the blitter counts), move.applescript, errors.txt (what
# the tools said).
#
# In the environment: QEBENCH_SECS (10) a phase's length; QEBENCH_SAMPLE_MS
# (10) the profile's interval, 0 for no profile (sampling slows the window
# server a little: compare runs made the same way); QEBENCH_GL (~/gl);
# QEBENCH_X, QEBENCH_Y (120, 120) where the window goes; QEBENCH_TITLE_DY
# (-11) the title bar's middle, from the top the Finder reports;
# QEBENCH_EXPOSE_KEY (101, F9) the key code of "all windows".
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
label=$1; out=$2
if [ -z "$label" ] || [ -z "$out" ]; then
	echo "usage: sudo $0 <label> <output directory>" >&2
	exit 2
fi
if [ "`id -u`" != 0 ]; then
	echo "qebench: run me with sudo: sample(1) may only look into the window server as root" >&2
	exit 2
fi

secs=${QEBENCH_SECS:-10}
sample_ms=${QEBENCH_SAMPLE_MS:-10}
X=${QEBENCH_X:-120}; Y=${QEBENCH_Y:-120}; W=600; H=400
title_dy=${QEBENCH_TITLE_DY:--11}
expose_key=${QEBENCH_EXPOSE_KEY:-101}
# One drag: 40 steps 20 ms apart, and the 0.6 s drag itself waits.
drag_steps=40; drag_ms=20
ga_stats=/tmp/rdnga.stats

# The user at the console: whoever called sudo.
user=$SUDO_USER
[ "$user" = root ] && user=
if [ -n "$user" ]; then home=`eval echo "~$user"`; else home=$HOME; fi
gl=${QEBENCH_GL:-$home/gl}

mkdir -p "$out" || exit 1
base="$out/$label"
err="$base.errors.txt"
: > "$err"
: > "$base.raw.txt"

asuser() {
	if [ -n "$user" ]; then sudo -u "$user" "$@"; else "$@"; fi
}

isnum() {
	case $1 in ''|-|*[!0-9-]*) return 1 ;; esac
	return 0
}

now() {
	perl -MTime::HiRes=time -e 'printf("%.2f\n", time)'
}

# "pid seconds" of the window server; ps prints CPU time as [h:]m:s.hh.
ws_now() {
	ps -axc -o pid,time,command | awk '
		$3 == "WindowServer" {
			n = split($2, t, ":"); s = 0
			for (i = 1; i <= n; i++) s = s * 60 + t[i]
			printf("%d %.2f\n", $1, s); exit
		}'
}

ws_cpu() {
	set -- `ws_now`
	echo ${2:-n/a}
}

# The fence the GPU has reached. A fences without "now" takes it for zero
# seconds and prints its sentence at once: read the number from that too.
fence_now() {
	f=`asuser "$gl/fences" now 2>> "$err"` || { echo n/a; return; }
	case $f in
	*'-> '*) f=`echo "$f" | sed 's/.*-> \([0-9]*\)).*/\1/'` ;;
	esac
	if isnum "$f"; then echo $f; else echo n/a; fi
}

# Calls of the 2D plug-in's three blitters so far, if it is counting.
ga_calls() {
	if [ -r "$ga_stats" ]; then
		awk '{ n += $3 } END { printf("%.0f\n", n) }' "$ga_stats"
	else
		echo n/a
	fi
}

# later - earlier, with so many decimals
minus() {
	if isnum "${1%%.*}" && isnum "${2%%.*}"; then
		awk -v a="$1" -v b="$2" -v d="$3" 'BEGIN { printf("%." d "f\n", b - a) }'
	else
		echo n/a
	fi
}

surface_counts() {
	ioreg -c RadeonNISurfaceClient -w0 2>> "$err" |
		grep -E '"RadeonNI(Calls|Control|Flush)"' | sed 's/^[ |]*//'
}

# The Finder's window: opened in front, at a known place. Prints its ID.
finder_open() {
	asuser osascript 2>> "$err" <<EOF
tell application "Finder"
	activate
	set w to make new Finder window to startup disk
	set bounds of w to {$X, $Y, $((X + W)), $((Y + H))}
	return id of w
end tell
EOF
}

# "x y" of the window as the Finder reports it
finder_position() {
	asuser osascript -e "tell application \"Finder\" to get position of Finder window id $win" 2>> "$err" | tr -d ','
}

finder_place() {
	asuser osascript -e "tell application \"Finder\" to set position of Finder window id $win to {$1, $2}" > /dev/null 2>> "$err"
}

cleanup() {
	if [ -n "$win" ]; then
		asuser osascript -e "tell application \"Finder\" to close Finder window id $win" > /dev/null 2>> "$err"
		win=
	fi
}

# Each do_* is a phase's work and prints what it did, for the table.

# The clock AppleScript has counts whole seconds: start on a tick, so that
# the moves fill exactly the time. Diagonally out and back, 8 pixels a step.
do_move() {
	if [ -z "$win" ]; then echo "failed: no Finder window"; return; fi
	cat > "$base.move.applescript" <<EOF
set n to 0
set t to current date
repeat while (current date) = t
end repeat
set t0 to current date
repeat while ((current date) - t0) < $secs
	set k to n mod 100
	if k > 50 then set k to 100 - k
	tell application "Finder" to set position of Finder window id $win to {$wx + k * 8, $wy + k * 3}
	set n to n + 1
end repeat
return n
EOF
	n=`asuser osascript "$base.move.applescript" 2>> "$err"`
	if isnum "$n"; then
		echo "$n moves in $secs s"
	else
		echo "failed: AppleScript could not move the window"
	fi
}

# Before the drag is timed: does the window follow the pointer at all? If
# the title bar is not where it is taken to be, the press lands on
# something else, and that must not be dragged about for ten seconds.
drag_check() {
	drag_fail=
	if [ -z "$win" ]; then drag_fail="no Finder window"; return; fi
	if [ ! -x "$gl/drag" ]; then drag_fail="no $gl/drag"; return; fi
	ax=$((wx + W / 2)); ay=$((wy + title_dy))
	bx=$((ax + 300)); by=$((ay + 120))
	asuser "$gl/drag" $ax $ay $((ax + 40)) $ay 4 30 >> "$err" 2>&1
	set -- `finder_position`
	if ! isnum "$1" || [ $(($1 - wx)) -lt 20 ]; then
		drag_fail="the window did not follow the pointer (title bar not at $ax,$ay?)"
	fi
	finder_place $wx $wy
}

do_drag() {
	if [ -n "$drag_fail" ]; then echo "failed: $drag_fail"; return; fi
	n=0; end=`date +%s`; end=$((end + secs))
	while [ `date +%s` -lt $end ]; do
		asuser "$gl/drag" $ax $ay $bx $by $drag_steps $drag_ms >> "$err" 2>&1 || break
		asuser "$gl/drag" $bx $by $ax $ay $drag_steps $drag_ms >> "$err" 2>&1 || break
		n=$((n + 2))
	done
	echo "$n drags by 300,120 pixels, $drag_steps steps $drag_ms ms apart"
}

expose() {
	asuser osascript -e "tell application \"System Events\" to key code $expose_key" > /dev/null 2>> "$err"
}

# Whether Expose really happened cannot be seen from here: compare the
# phase's numbers with idle's, or look.
do_expose() {
	n=0; end=`date +%s`; end=$((end + secs))
	while [ `date +%s` -lt $end ]; do
		if ! expose; then
			echo "failed: System Events did not take the key (after $n times)"
			return
		fi
		sleep 1
		if ! expose; then
			echo "failed: System Events did not take the second key; Expose may be showing"
			return
		fi
		sleep 1
		n=$((n + 1))
	done
	echo "$n times in and out (key code $expose_key)"
}

do_idle() {
	sleep $secs
	echo "-"
}

# phase <name> <function>: measure around the work, add a row to the table.
phase() {
	name=$1
	spid=
	c0=`ws_cpu`; f0=`fence_now`; g0=`ga_calls`; t0=`now`
	if [ "$sample_ms" != 0 ] && [ -n "$wspid" ]; then
		sample $wspid $secs $sample_ms -file "$base.$name.sample.txt" > /dev/null 2>> "$err" &
		spid=$!
	fi
	work=`$2`
	t1=`now`; c1=`ws_cpu`; f1=`fence_now`; g1=`ga_calls`
	[ -n "$spid" ] && wait $spid
	surface_counts > "$base.$name.surface.txt"
	[ -r "$ga_stats" ] && cp "$ga_stats" "$base.$name.ga.txt"
	wall=`minus $t0 $t1 1`; cpu=`minus $c0 $c1 2`
	note=
	if [ "$sample_ms" != 0 ] && [ ! -s "$base.$name.sample.txt" ]; then
		note=" (no profile)"
	fi
	echo "phase=$name t0=$t0 t1=$t1 ws_cpu0=$c0 ws_cpu1=$c1 fence0=$f0 fence1=$f1 blits0=$g0 blits1=$g1 work=\"$work\"" >> "$base.raw.txt"
	printf '%-7s %7s %9s %9s %7s  %s\n' $name "$wall" "$cpu" "`minus $f0 $f1 0`" \
		"`minus $g0 $g1 0`" "$work$note" >> "$base.summary.txt"
}

set -- `ws_now`
wspid=$1
{
	echo "qebench $label, `date '+%Y-%m-%d %H:%M:%S'`, `sw_vers -productVersion 2> /dev/null` `uname -m`"
	if [ -x "$gl/qe" ]; then asuser "$gl/qe" 2>> "$err"; else echo "no $gl/qe: Quartz Extreme not asked about"; fi
	echo "window server: pid ${wspid:-not found}; phases of $secs s; profile every $sample_ms ms (0: none)"
	echo "GPU: `fence_now` (the fence reached; n/a: no accelerator, or no $gl/fences)"
	echo "blitter calls: `ga_calls` (n/a: /tmp/rdnga.stats is not there, the plug-in is not counting)"
	echo
	printf '%-7s %7s %9s %9s %7s  %s\n' phase "wall s" "WS cpu s" "GPU bufs" blits work
} > "$base.summary.txt"
surface_counts > "$base.start.surface.txt"

trap 'cleanup; exit 1' 1 2 15
# System Events takes a while to start the first time it is told something.
asuser osascript -e 'tell application "System Events" to launch' > /dev/null 2>> "$err"
win=`finder_open`
if isnum "$win"; then
	sleep 2
	set -- `finder_position`
	wx=$1; wy=$2
	if ! isnum "$wx" || ! isnum "$wy"; then
		cleanup
	fi
else
	echo "qebench: could not open a Finder window (is $user logged in at the console?); see $err" >&2
	win=
fi

phase move do_move
[ -n "$win" ] && finder_place $wx $wy
drag_check
sleep 1
phase drag do_drag
[ -n "$win" ] && finder_place $wx $wy
sleep 1
phase expose do_expose
sleep 2
phase idle do_idle
cleanup

[ -n "$user" ] && chown "$user" "$out" "$base".*
cat "$base.summary.txt"

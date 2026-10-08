#!/usr/bin/perl
# Run piglit tests one after another on the Mac and record each result.
# Runs under Tiger's perl 5.8; started by tools/piglit/run.py.
#
#   run.pl <list.tsv> <outdir> <timeout-seconds>
#
# The list holds "name<TAB>command"; commands run in the current directory
# (the piglit root) with PIGLIT_SOURCE_DIR set to it. <outdir>/results gets
# one line per test: name, result, exit status, seconds. Tests already in it
# are skipped, so a run that died resumes. The output of anything that did
# not pass goes in <outdir>/logs. <outdir>/stop ends the run after the test
# in progress.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
use strict;
use POSIX qw(:sys_wait_h setsid);
use Time::HiRes qw(time sleep);
use Cwd;

my ($list, $out, $limit) = @ARGV;
$limit ||= 60;
my $root = getcwd();
$ENV{PIGLIT_SOURCE_DIR} = $root;
$ENV{DYLD_LIBRARY_PATH} = "$root/lib";
mkdir $out; mkdir "$out/logs";

my %done;
if (open(my $r, "<", "$out/results")) {
    while (<$r>) { my ($n) = split /\t/; $done{$n} = 1; }
    close $r;
}
open(my $res, ">>", "$out/results") or die "results: $!";
select((select($res), $| = 1)[0]);
open(my $in, "<", $list) or die "list: $!";

while (my $line = <$in>) {
    chomp $line;
    my ($name, $cmd) = split /\t/, $line, 2;
    next if !defined $cmd || $done{$name};
    last if -e "$out/stop";
    (my $file = $name) =~ s/[^A-Za-z0-9_.-]/_/g;
    my $log = "$out/logs/$file.txt";
    my $t0 = time;
    my $pid = fork();
    die "fork: $!" unless defined $pid;
    if (!$pid) {
        setsid();
        open(STDIN, "<", "/dev/null");
        open(STDOUT, ">", $log) or exit 126;
        open(STDERR, ">&", \*STDOUT);
        exec("/bin/sh", "-c", $cmd);
        exit 127;
    }
    my ($status, $timedout) = (undef, 0);
    while (1) {
        my $w = waitpid($pid, WNOHANG);
        if ($w == $pid) { $status = $?; last; }
        if (time - $t0 > $limit) {
            kill 9, -$pid; kill 9, $pid;
            waitpid($pid, 0);
            $status = $?; $timedout = 1; last;
        }
        sleep 0.05;
    }
    my $result = "crash";
    my $text = "";
    if (open(my $l, "<", $log)) { local $/; $text = <$l>; close $l; }
    if ($text =~ /PIGLIT: \{"result": "(\w+)"/) { $result = $1; }
    $result = "timeout" if $timedout;
    my $sig = $status & 127;
    my $code = $status >> 8;
    $result = "crash" if $sig && !$timedout;
    printf $res "%s\t%s\t%d\t%.2f\n", $name, $result, $sig ? 128 + $sig : $code, time - $t0;
    if ($result eq "pass" || $result eq "skip") { unlink $log; }
    elsif (length($text) > 20000) {
        open(my $l, ">", $log); print $l substr($text, 0, 20000); close $l;
    }
}
open(my $d, ">", "$out/finished"); print $d time, "\n"; close $d;

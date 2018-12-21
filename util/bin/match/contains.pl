#!/usr/bin/env perl
# Copyright 2018 Schibsted

use strict;

open HS, '<', $ARGV[0] or die "Failed to open $ARGV[0]";
open NS, '<', $ARGV[1] or die "Failed to open $ARGV[1]";

my @haystack = <HS>;
my @needles = <NS>;
my @compiled;

for (@needles) {
	chomp;
	my $re;
	eval {
		$re = qr/^$_\z/;
	};
	push @compiled, $re;
}

for my $line (@haystack) {
	chomp $line;
	my $match = -1;
	for my $i (0 .. $#needles) {
		$match = $i if $line eq $needles[$i];
		$match = $i if $compiled[$i] && $line =~ $compiled[$i];
		last if $match != -1;
	}
	next if $match == -1;
	splice @needles, $match, 1;
	splice @compiled, $match, 1;
}

if (@needles) {
	print "These lines were not found in $ARGV[0]:\n";
	print join("\n", @needles);
	print "\n";
	exit 1;
}

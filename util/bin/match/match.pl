#!/usr/bin/perl -w
# Copyright 2018 Schibsted
#
# match.pl
#
# Do a linewise diff between two files, where the second file may contain regexp.
#
# Due to the inherent ambiguity over when and what to escape for the user to get
# the expected matching, an alternative matcher is included and automatically
# used if a line contains a section of the form {{<regexp here>}}. When found,
# everything _outside_ of these clauses will have characters in the class of
# regex-operators escaped.
#
# This mode can be forced using the --force-new option.
#
# Example:
#		Output		Expected	Comment
# Old style:	"Hej!" 		"Hej."		Will pass, becuase . is a regexp operator.
# New style	"Hej!"		"Hej."		Will not pass in new-mode, because the . will be escaped to \.
#
use strict;
use Getopt::Long;
use POSIX qw(strftime);
use Term::ANSIColor;

use constant SEG_LITERAL => 0;
use constant SEG_REGEX   => 1;

my $DEBUG = 0;
my $force_new = 0;
my $ignore_time = 0;
my $ignore_token = 0;
my $lineno = 0;

chomp(my $cwd = `pwd`);
my $pwd = '[' . color('bold red') . $cwd . color('reset') . ']';
my $diff_tool = ($ENV{'DIFF_TOOL'} or 'vimdiff');

sub diffinfoexit($$@)
{
	my ($f1, $f2, $l) = @_;
	$f2 =~ s/([^\\])([&?: ])/$1\\$2/g; # escape &, ?, : and space for shell
	if ( defined($l) ) {
		$l =~ s/([0-9]+)/ +$1/;	# add line with difference when calling vimdiff
	} else {
		$l = "";
	}
	my $diff_flags = ($ENV{'DIFF_FLAGS'} or '-C2');
	print ": diff OUTPUT $pwd ; $diff_tool$l $f1 $f2\n";
	system "/usr/bin/diff $diff_flags $f1 $f2";
	exit(1);
}

# Escape perl regular-expression operators and special characters.
sub regescape($)
{
	my $s = shift;
	$s =~ s/([\(\)\[\]\{\}+*.?\^\$\\|])/\\$1/g;
	return $s;
}

# Concatenate all segments into a string, escaping the literal ones.
sub pkgtostring($)
{
	my $packet = shift;
	my $s = '';

	foreach my $seg (@{$packet}) {
		if($seg->{TYPE} == SEG_LITERAL) {
			$s .= regescape($seg->{PAYLOAD});
		} else {
			$s .= $seg->{PAYLOAD};
		}
	}

	return $s;
}

# Used to auto-detect the use of the new-mode matcher
sub has_regex_seg($)
{
	my $s = shift;
	my $o1;
	if (($o1 = index($s, '{{')) > -1) {
		return index($s, '}}', $o1) > -1 ? 1 : 0;
	}
	return 0;
}

#
# Create array of hashes (segments)
#
sub parse_regexped_line($)
{
	my $s = shift;
	my @result;

	while(length($s) > 0) {
		print "Parsing '".$s."'\n" if $DEBUG;	
		# scan for first regexp-clause
		my $rlim = index($s, '{{');
		if($rlim > 0) { # Found a packet, after som literal data.
			print "Segment start found at ".$rlim."\n" if $DEBUG;	
			push(@result, { TYPE => SEG_LITERAL, PAYLOAD => substr($s, 0, $rlim)});
			substr($s,0,$rlim) = '';
		}

		if(substr($s,0,2) eq '{{') {
			print "Found regexp packet in '".$s."'\n" if $DEBUG;
			substr($s, 0, 2) = ''; # eat
			$rlim = index($s, '}}');
			if($rlim > 0) {
				push(@result, { TYPE => SEG_REGEX, PAYLOAD => substr($s, 0, $rlim)});
				substr($s, 0, $rlim+2) = '';
			} else {
				print "PARSE ERROR. Expected closing '}}', none found.\n";
				return undef;
			}
  		} else { # rest of line when line has no more {{ ...
			print "Final segment is '".$s."'\n" if $DEBUG;
			push(@result, { TYPE => SEG_LITERAL, PAYLOAD => $s});
			$s = '';	
		}
	}
	return \@result;
}


sub match_line($$$)
{
	my $l1 = shift;
	my $l2 = shift;
	my $display_nomatch = shift;

 	my $rxpkg = parse_regexped_line($l2); 
	my $l2escaped = pkgtostring($rxpkg);
 
	if ($l1 !~ /^$l2escaped\z/) {
		print "NEWDIFF:$lineno: '$l1' <> '".$l2escaped."'\n" if $display_nomatch;
		return 0;
	}

	return 1;
}

GetOptions (
	"debug" => \$DEBUG,
	"force-new" => \$force_new,
	"ignore-time" => \$ignore_time,
	"ignore-token" => \$ignore_token,
);

my @l1;
my @l2;
my ($f1, $f2) = @ARGV;
if((!defined $f1) or (!defined $f2)) {
	print STDERR "$0: [--debug] [--force-new] [--ignore-time] [--ignore-token] diff_left diff_right\n";
	exit 1;
}

my $today = strftime('%Y-%m-%d', localtime);

if ($f1 ne '-') {
	open F1, "<$f1" or die "Failed to open $f1 $pwd";
	@l1 = <F1>;
	close F1;
} else {
	@l1 = <STDIN>;
}

open F2, "<$f2" or die "Failed to open $f2 $pwd";
@l2 = <F2>;
close F2;

while (defined (my $l1 = shift @l1)) {
	chomp($l1);
	$lineno++;

	if (defined(my $l2 = shift(@l2))) {
		chomp($l2);

		my $optional = 0;

		($optional, $l2) = ($1, $2) if $l2 =~ /^(\?\?\?)(.*)$/;

		if ($l1 ne $l2) {
			if ($force_new || has_regex_seg($l2)) {
				if( !match_line($l1,$l2,1) ) {
					print ": diff OUTPUT $pwd ; $diff_tool $f1 $f2\n";
					exit 1;
				}
			} else {
				if ($l1 !~ /^($l2)\z/) {
					next if ($ignore_token and $l1 =~ m/^token:.+$/ and $l2 =~ m/^token:.+$/);
					next if ($ignore_time and index($l1, ":" . $today) >= 0);

					my $tmp = $l2;
					$tmp =~ s/([\(\)\|])/\\$1/g;
					if ($l1 !~ /^($tmp)\z/) {

						if ($optional) {
							unshift @l1, $l1;
							next;
						}

						print "DIFF:$lineno: $l1 <> $l2\n";
						diffinfoexit($f1, $f2, $lineno);
					}
				}
			}

		}
	} else {
		print "File '$f1' has ", int($#l1 + 2), " more lines. $pwd\n";
		for (@l1) {
			if ($_ ne ".*\n" && $_ !~ /^\?\?\?/) {
				print ">$_";
				diffinfoexit($f1, $f2, $#l1);
			}
		}
		exit(1);
	}
}

if ($#l2 >= 0) {
	for (@l2) {
		if ($_ ne ".*\n" && $_ !~ /^\?\?\?/) {
			print "File '$f2' has ", int($#l2 + 1), " more lines. $pwd\n";
			print ">$_";
			diffinfoexit($f1, $f2, $#l2);
		}
	}
	exit(1);
}

exit 0;

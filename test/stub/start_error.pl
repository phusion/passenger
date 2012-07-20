#!/usr/bin/env perl
use strict;
use IO::Handle;

STDOUT->autoflush(1);
STDERR->autoflush(1);
print("!> I have control 1.0\n");
die("Invalid initialization header") if (<STDIN> ne "You have control 1.0\n");

my %options = {};
while ((my $line = <STDIN>) ne "\n") {
	$line =~ s/\n//;
	my ($name, $value) = split(/: */, $line, 2);
	$options{$name} = $value;
}

print("!> Error\n");
print("!> \n");
if ($ARGV[0] eq 'freeze') {
	sleep(1000);
} else {
	print("He's dead, Jim!\n");
	print("Relax, I'm a doctor.\n");
}

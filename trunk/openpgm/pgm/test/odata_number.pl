#!/usr/bin/perl
# odata_number.pl
# 5.1.1. Maximum Cumulative Transmit Rate

use strict;
use PGM::Test;

BEGIN { require "test.conf.pl"; }

$| = 1;

my $mon = PGM::Test->new(tag => 'mon', host => $config{mon}{host}, cmd => $config{mon}{cmd});
my $app = PGM::Test->new(tag => 'app', host => $config{app}{host}, cmd => $config{app}{cmd});

$mon->connect;
$app->connect;

sub close_ssh {
	$mon = $app = undef;
	print "finished.\n";
}

$SIG{'INT'} = sub { print "interrupt caught.\n"; close_ssh(); };

$mon->say ("filter $config{app}{ip}");
print "mon: ready.\n";

$app->say ("create ao");
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");

print "app: send 1000 data packets ...\n";
# hide stdout
open(OLDOUT, ">&STDOUT");
open(STDOUT, ">/dev/null") or die "Can't redirect stdout: $!";

for (0..999)
{
	my $i = $_;
	$app->say ("send ao $i");
	my $odata = $mon->wait_for_odata;

	die "out of sequence ODATA, received $odata->{PGM}->{odSqn} expected $i\n" unless $odata->{PGM}->{odSqn} == $i;
}

# restore stdout
close(STDOUT) or die "Can't close STDOUT: $!";
open(STDOUT, ">&OLDOUT") or die "Can't restore stdout: $!";
close(OLDOUT) or die "Can't close OLDOUT: $!";

print "mon: received 1000 x odata.\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

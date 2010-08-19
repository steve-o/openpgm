#!/usr/bin/perl
# odata_number.pl
# 5.1.1. Maximum Cumulative Transmit Rate

use strict;
use Time::HiRes qw( gettimeofday tv_interval );
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
$app->say ("set ao TXW_MAX_RTE 1500");
$app->say ("bind ao");
$app->say ("connect ao");

print "app: send 50 data packets ...\n";
my $t0 = [gettimeofday];

# hide stdout
open(OLDOUT, ">&STDOUT");
open(STDOUT, ">/dev/null") or die "Can't redirect stdout: $!";

my $payload = "ringo" x 100;
my $bytes = 0;
for (1..50)
{
	$app->say ("send ao $payload");
	my $odata = $mon->wait_for_odata;
	$bytes += $odata->{IP}->{length};
}

close(STDOUT) or die "Can't close STDOUT: $!";
open(STDOUT, ">&OLDOUT") or die "Can't restore stdout: $!";
close(OLDOUT) or die "Can't close OLDOUT: $!";

my $elapsed = tv_interval ( $t0, [gettimeofday] );
print "mon: received 50 x odata, $bytes bytes in $elapsed seconds.\n";

my $rate = $bytes / $elapsed;
$rate = $bytes if ($rate > $bytes);
print "mon: incoming data rate $rate bps.\n";

die "incoming rate exceeds set TXW_MAX_RTE\n" unless $rate < 1650;

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

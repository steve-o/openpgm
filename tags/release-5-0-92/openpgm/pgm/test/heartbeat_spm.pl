#!/usr/bin/perl
# heartbeat_spm.pl
# 5.1.5. Heartbeat SPMs

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
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");

print "app: publish test data.\n";
$app->say ("send ao ringo");

print "mon: wait for odata ...\n";
$mon->wait_for_odata;
my $t0 = [gettimeofday];

for (1..4)	# look for four consecutive heartbeat SPMs less than 5000ms apart
{
	print "mon: wait for spm ...\n";
	$mon->wait_for_spm ({ 'timeout' => 5 });
	my $tn = [gettimeofday];
	my $elapsed = tv_interval ( $t0, $tn );
	$t0 = $tn;

	print "mon: spm received after $elapsed seconds.\n";
}

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

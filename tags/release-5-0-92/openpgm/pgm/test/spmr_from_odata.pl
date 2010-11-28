#!/usr/bin/perl
# spmr_from_odata.pl
# 13.3.1. SPM Requests

use strict;
use PGM::Test;

BEGIN { require "test.conf.pl"; }

$| = 1;

my $mon = PGM::Test->new(tag => 'mon', host => $config{mon}{host}, cmd => $config{mon}{cmd});
my $sim = PGM::Test->new(tag => 'sim', host => $config{sim}{host}, cmd => $config{sim}{cmd});
my $app = PGM::Test->new(tag => 'app', host => $config{app}{host}, cmd => $config{app}{cmd});

$mon->connect;
$sim->connect;
$app->connect;

sub close_ssh {
	$mon = $sim = $app = undef;
	print "finished.\n";
}

$SIG{'INT'} = sub { print "interrupt caught.\n"; close_ssh(); };

$mon->say ("filter $config{app}{ip}");
print "mon: ready.\n";

$app->say ("create ao");
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");

$sim->say ("create fake ao");
$sim->say ("bind ao");
$sim->say ("connect ao");
print "sim: publish ODATA sqn 90,001.\n";
$sim->say ("net send odata ao 90001 90001 ringo");

my $data = $app->wait_for_data;
print "app: received data [$data].\n";

print "mon: wait for SPMR ...\n";
$mon->wait_for_spmr;
print "mon: received SPMR.\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

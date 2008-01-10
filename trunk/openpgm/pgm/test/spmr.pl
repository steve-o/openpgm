#!/usr/bin/perl
# spmr.pl
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

$sim->say ("create fake ao");
$sim->say ("bind ao");
print "sim: ready.\n";

$app->say ("create ao");
$app->say ("bind ao");

## capture GSI of test spp
$app->say ("send ao nashi");
print "mon: wait for odata ...\n";
my $odata = $mon->wait_for_odata;
print "mon: odata received.\n";

print "sim: request SPM via SPMR.\n";
$sim->say ("net send spmr ao $odata->{PGM}->{gsi}.$odata->{PGM}->{sourcePort}");

print "mon: wait for SPM ...\n";
$mon->wait_for_spm;
print "mon: SPM received.\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

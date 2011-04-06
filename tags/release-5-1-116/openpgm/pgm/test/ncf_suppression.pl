#!/usr/bin/perl
# ncf_suppression.pl
# 6.3. Data Recovery by Negative Acknowledgment

use strict;
use Time::HiRes qw( gettimeofday tv_interval );
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

## capture GSI of test spp
$app->say ("send ao nashi");
print "mon: wait for odata ...\n";
my $odata = $mon->wait_for_odata;
print "mon: odata received.\n";

$sim->say ("create fake ao");
$sim->say ("bind ao");
$sim->say ("connect ao");

print "sim: publish SPM txw_trail 90,001 txw_lead 90,000 at spm_sqn 3200.\n";
$sim->say ("net send spm ao 3200 90001 90000");

# no NAKs should be generated.
print "sim: waiting 2 seconds for erroneous NAKs ...\n";
$sim->die_on_nak ({ 'timeout' => 2 });
print "sim: no NAKs received.\n";

print "sim: publish ODATA sqn 90,001.\n";
$sim->say ("net send odata ao 90001 90001 ringo");

print "app: wait for data ...\n";
my $data = $app->wait_for_data;
print "app: data received [$data].\n";

# no NAKs should be generated.
print "sim: waiting 2 seconds for erroneous NAKs ...\n";
$sim->die_on_nak ({ 'timeout' => 2 });
print "sim: no NAKs received.\n";

## first run through with regular NAK generation to get regular backoff interval
print "sim: publish ODATA sqn 90,003.\n";
$sim->say ("net send odata ao 90003 90001 ichigo");
my $t0 = [gettimeofday];
print "sim: waiting for valid NAK.\n";
$sim->wait_for_nak;
my $normal_backoff = tv_interval ( $t0, [gettimeofday] );
print "sim: NAK received in $normal_backoff seconds.\n";

## cleanup by publishing repair data
print "sim: publish RDATA sqn 90,002.\n";
$sim->say ("net send odata ao 90002 90001 momo");
print "app: wait for data ...\n";
my $data = $app->wait_for_data;
print "app: data received [$data].\n";

## second run with NAK suppression
$t0 = [gettimeofday];
print "sim: publish ODATA sqn 90,005.\n";
$sim->say ("net send odata ao 90005 90001 anzu");
print "sim: publish NCF sqn 90,004.\n";
$sim->say ("net send ncf ao $odata->{PGM}->{gsi}.$odata->{PGM}->{sourcePort} 90004");

print "sim: waiting for valid NAK.\n";
$sim->wait_for_nak;
my $suppressed_backoff = tv_interval ( $t0, [gettimeofday] );
print "sim: NAK received in $suppressed_backoff seconds.\n";

die "NAK suppression failed.\n" unless ($suppressed_backoff > $normal_backoff);

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

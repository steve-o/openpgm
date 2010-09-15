#!/usr/bin/perl
# nak_parity.pl
# 5.3. Repairs

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

$sim->say ("create ao");
$sim->say ("set ao FEC RS(255,4)");
$sim->say ("bind ao");
print "sim: ready.\n";

$app->say ("create ao");
$app->say ("set ao FEC RS(255,4)");
$app->say ("bind ao");
$app->say ("listen ao");	# to process NAK requests

print "app: publish test data.\n";
$app->say ("send ao ringo");
$app->say ("send ao ichigo");
$app->say ("send ao momo");
$app->say ("send ao budo");
$app->say ("send ao nashi");
$app->say ("send ao anzu");
$app->say ("send ao kaki");

my $odata = undef;
my $ocnt = 0;
for (1..7) {
	print "mon: wait for odata ...\n";
	$odata = $mon->wait_for_odata;
	$ocnt++;
	print "mon: received $ocnt x odata.\n";
}

print "sim: send nak to app (transmission group = 0, packet count = 1).\n";
$sim->say ("net send parity nak ao $odata->{PGM}->{gsi}.$odata->{PGM}->{sourcePort} 1");

print "mon: wait for rdata ...\n";
my $rdata = $mon->wait_for_rdata;
print "mon: rdata received.\n";

die "Selective RDATA received, parityPacket=false\n" unless $rdata->{PGM}->{options}->{parityPacket};
print "Parity RDATA received.\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

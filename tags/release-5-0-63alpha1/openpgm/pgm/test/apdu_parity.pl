#!/usr/bin/perl
# apdu_parity.pl
# 6.1. Data Reception

use strict;
use PGM::Test;

BEGIN { require "test.conf.pl"; }

$| = 1;

my $sim = PGM::Test->new(tag => 'sim', host => $config{sim}{host}, cmd => $config{sim}{cmd});
my $app = PGM::Test->new(tag => 'app', host => $config{app}{host}, cmd => $config{app}{cmd});

$sim->connect;
$app->connect;

sub close_ssh {
	$sim = $app = undef;
	print "finished.\n";
}

$SIG{'INT'} = sub { print "interrupt caught.\n"; close_ssh(); };

$app->say ("create ao");
##$app->say ("set ao FEC RS(255,4)");
$app->say ("bind ao");
$app->say ("listen ao");

$sim->say ("create ao");
$sim->say ("set ao FEC RS(255,4)");
$sim->say ("bind ao");

print "sim: publish APDU.\n";
$sim->say ("send brokn ao ringo x 1200");

print "sim: insert parity NAK from app.\n";

#print "sim: wait for NAK.\n";
#my $nak = $sim->wait_for_nak;
#die "Selective NAK received, parityPacket=false\n" unless $nak->{PGM}->{options}->{parityPacket};
#print "Parity NAK received.\n";

print "sim: insert parity RDATA from sim.\n";

print "app: wait for data ...\n";
my $data = $app->wait_for_data;
print "app: received data [$data].\n";

my $ref_data = "ringo" x 1200;
die "incoming data corrupt\n" unless ($data == $ref_data);

print "test completed successfully.\n";

$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

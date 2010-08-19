#!/usr/bin/perl
# nak_repeat.pl
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
$sim->say ("bind ao");
$sim->say ("connect ao");
print "sim: ready.\n";

$app->say ("create ao");
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");	# to process NAK requests

print "app: publish test data.\n";
$app->say ("send ao " . ("ringo" x 200));
$app->say ("send ao " . ("ichigo" x 200));
$app->say ("send ao " . ("momo" x 200));

my $odata = undef;
my $ocnt = 0;
for (1..3) {
	print "mon: wait for odata ...\n";
	$odata = $mon->wait_for_odata;
	$ocnt++;
	print "mon: received $ocnt x odata.\n";
}

for (1..1000) {
	my $i = $_;
	print "sim: $i# send nak to app.\n";
	$sim->say ("net send nak ao $odata->{PGM}->{gsi}.$odata->{PGM}->{sourcePort} 2");

	print "mon: $i# wait for rdata ...\n";
	$mon->wait_for_rdata;
	print "mon: $i# rdata received.\n";
}

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

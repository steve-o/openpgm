#!/usr/bin/perl
# nak_list.pl
# 9.3. NAK List Option - OPT_NAK_LIST

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
$app->say ("listen ao");

print "app: publish test data.\n";
$app->say ("send ao ringo");
$app->say ("send ao ichigo");
$app->say ("send ao momo");

my $odata = undef;
my $ocnt = 0;
for (1..3) {
	print "mon: wait for odata ...\n";
	$odata = $mon->wait_for_odata;
	$ocnt++;
	print "mon: received $ocnt x odata.\n";
}

print "sim: send nak to app.\n";
$sim->say ("net send nak ao $odata->{PGM}->{gsi}.$odata->{PGM}->{sourcePort} 0,1,2");

my $rcnt = 0;
for (1..3) {
	print "mon: wait for rdata ...\n";
	$mon->wait_for_rdata;
	$rcnt++;
	print "mon: received $rcnt x rdata.\n";
}

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

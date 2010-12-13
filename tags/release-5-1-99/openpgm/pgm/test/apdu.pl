#!/usr/bin/perl
# apdu.pl
# 6.1. Data Reception

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

$app->say ("set network $config{app}{network}");
$app->say ("create ao");
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");

$sim->say ("set network $config{sim}{network}");
$sim->say ("create ao");
$sim->say ("bind ao");
$sim->say ("connect ao");

print "sim: publish APDU.\n";
$sim->say ("send ao ringo x 1000");

print "app: wait for data ...\n";
my $data = $app->wait_for_data;
print "app: received data [$data].\n";

my $ref_data = "ringo" x 1000;
die "incoming data corrupt\n" unless ($data == $ref_data);

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

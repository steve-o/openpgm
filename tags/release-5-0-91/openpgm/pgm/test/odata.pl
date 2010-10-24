#!/usr/bin/perl
# odata.pl
# 3.6.2.1. ODATA - Original Data

use strict;
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

print "app: publish test data.\n";
$app->say ("send ao ringo");

print "mon: wait for odata ...\n";
$mon->wait_for_odata;
print "mon: received odata.\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

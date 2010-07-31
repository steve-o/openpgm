#!/usr/bin/perl
# on-demand_spm.pl 
# 5.1.4. Ambient SPMs with on-demand parity flag

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
$app->say ("set ao FEC RS(255,64)");
$app->say ("bind ao");
print "app: ready.\n";

print "mon: wait for spm ...\n";
my $spm = $mon->wait_for_spm;
print "mon: received spm.\n";

die "SPM does not contain any PGM options\n" unless $spm->{PGM}->{pgmOptions};
die "SPM does not contain a PGM_OPT_PARITY_PRM option\n" unless $spm->{PGM}->{pgmOptions}[1]->{type} =~ /OPT_PARITY_PRM/;
print "pro-active parity " . ($spm->{PGM}->{pgmOptions}[1]->{'P-bit'} ? 'enabled' : 'disabled') . ", P-bit " . ($spm->{PGM}->{pgmOptions}[1]->{'P-bit'} ? 'true' : 'false') . "\n";
die "on-demand parity disabled, O-bit false\n" unless $spm->{PGM}->{pgmOptions}[1]->{'O-bit'};
print "on-demand parity enabled, O-bit true\n";

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

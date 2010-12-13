#!/usr/bin/perl
# ncf_cancellation.pl
# 6.3. Data Recovery by Negative Acknowledgment

use strict;
use IO::Handle;
use JSON;
use Time::HiRes qw( gettimeofday tv_interval );
use PGM::Test;

BEGIN { require "test.conf.pl"; }

$| = 1;

my $mon = PGM::Test->new(tag => 'mon', host => $config{mon}{host}, cmd => $config{mon}{cmd});
my $sim = PGM::Test->new(tag => 'sim', host => $config{sim}{host}, cmd => $config{sim}{cmd});
my $app = PGM::Test->new(tag => 'app', host => $config{app}{host}, cmd => $config{app}{cmd});

pipe(FROM_PARENT, TO_CHILD) or die "pipe: $!";
FROM_PARENT->autoflush(1);

$mon->connect;
$sim->connect;
$app->connect;

sub close_ssh {
	close FROM_PARENT; close TO_CHILD;
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

print "sim: publish ODATA sqn 90,003.\n";
$sim->say ("net send odata ao 90003 90001 ichigo");
my $t0 = [gettimeofday];

if (my $pid = fork) {
# parent 
	close FROM_PARENT;

	print "app: wait for data ...\n";
	my $data = $app->wait_for_data({ 'timeout' => 0 });
	print "app: data received [$data].\n";

	print TO_CHILD "die\n";

	close TO_CHILD;
	waitpid($pid,0);
} else {
# child
	die "cannot fork: $!" unless defined $pid;
	close TO_CHILD;
	print "sim: loop waiting for NAKs ...\n";

	my $fh = $sim->{in};
	vec(my $rin, fileno(FROM_PARENT), 1) = 1;
	vec($rin, fileno($fh), 1) = 1;
	my $rout = undef;

	my $b = '';
	my $state = 0;
	my $json = new JSON;
	my $io = IO::Handle->new_from_fd( fileno($fh), "r" );
	my $cnt = 0;
	while (select($rout = $rin, undef, undef, undef))
	{
		last if( vec($rout, fileno(FROM_PARENT), 1) );
		last unless (defined($_ = $io->getline));
		chomp;
		my $l = $_;
		if ($state == 0) {
			if ($l =~ /{$/) {
				$state = 1;
			} else {
				print "sim [$l]\n";
			}
		}

		if ($state == 1) {
			$b .= $l;

			if ($l =~ /^}$/) {
				$state = 0;

				my $obj = $json->decode($b);
				if ($obj->{PGM}->{type} =~ /NAK/) {
					$cnt++;
					my $elapsed = tv_interval ( $t0, [gettimeofday] );
					print "sim: $cnt x NAK received in $elapsed seconds.\n";
				}

# reset
				$b = '';
			}
		}
	}

	print "sim: loop finished.\n";
	close FROM_PARENT;
	exit;
} 

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

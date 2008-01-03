#!/usr/bin/perl

use Net::SSH qw(sshopen2);
use POSIX ":sys_wait_h";
use JSON;

use strict;

my $app_host = 'ayaka';
my $app_ip = '10.6.28.31';
my $mon_host = 'kiku';
my $sim_host = 'sora';
my $mon = '/miru/projects/openpgm/pgm/ref/debug/test/monitor';
my $app = '/miru/projects/openpgm/pgm/ref/debug/test/app';
my $sim = '/miru/projects/openpgm/pgm/ref/debug/test/sim';


$| = 1;

# setup reaper for failed SSH connections

my %children = ();

sub REAPER {
	my $child;
	while (($child = waitpid(-1,WNOHANG)) > 0) {
		$children{$child} = $?;
	}
	$SIG{CHLD} = \&REAPER;
}
sub HUNTSMAN {
	local($SIG{CHLD}) = 'IGNORE';
	kill 'INT' => keys %children;
	exit;
}
$SIG{CHLD} = \&REAPER;

my $pid;
$pid = sshopen2 ($mon_host, *MON_READER, *MON_WRITER, "uname -a && sudo $mon") || die "ssh: $!";
$children{$pid} = 1;
$pid = sshopen2 ($sim_host, *SIM_READER, *SIM_WRITER, "uname -a && sudo $sim") || die "ssh: $!";
$children{$pid} = 1;
$pid = sshopen2 ($app_host, *APP_READER, *APP_WRITER, "uname -a && sudo $app") || die "ssh: $!";
$children{$pid} = 1;

sub close_ssh {
	print "closing ssh connections ...\n";
	close (MON_READER); close (MON_WRITER);
	close (SIM_READER); close (SIM_WRITER);
	close (APP_READER); close (APP_WRITER);
	print "finished.\n";
	HUNTSMAN();
}

$SIG{'INT'} = sub { print "interrupt caught.\n"; close_ssh(); };


# wait all spawned programs to become ready

sub wait_for_ready {
	my($fh,$tag) = @_;
	while (<$fh>) {
		print "$tag: $_";
		last if /^READY/;
	}
}

wait_for_ready (\*MON_READER, "mon");
print MON_WRITER "filter $app_ip\n";
wait_for_ready (\*MON_READER, "mon");
print "mon: ready.\n";

wait_for_ready (\*SIM_READER, "sim");
print SIM_WRITER "create baa\n";
wait_for_ready (\*SIM_READER, "sim");
print SIM_WRITER "bind baa\n";
wait_for_ready (\*SIM_READER, "sim");
print "sim: ready.\n";

wait_for_ready (\*APP_READER, "app");
print APP_WRITER "create moo\n";
wait_for_ready (\*APP_READER, "app");
print APP_WRITER "bind moo\n";
wait_for_ready (\*APP_READER, "app");
print "app: ready.\n";

# tell app to publish odata

print "app: publish data\n";
print APP_WRITER "send moo ringo\n";
wait_for_ready (\*APP_READER, "app");
print APP_WRITER "send moo ichigo\n";
wait_for_ready (\*APP_READER, "app");
print APP_WRITER "send moo momo\n";
wait_for_ready (\*APP_READER, "app");

sub wait_for_block {
	my($fh, $tag) = @_;
	my $b = '';
	my $state = 0;

	while (<$fh>) {
		chomp();
		my $l = $_;
		if ($state == 0) {
			if ($l =~ /^{$/) {
				$state = 1;
			} else {
				print "$tag: $l\n";
			}
		}

		if ($state == 1) {
			$b .= $l;

			if ($l =~ /^}$/) {
				$state = 0;
				return $b;
			}
		}
	}
}

sub wait_for_spm {
	my $fh = shift;
	my $json = new JSON;

	for (;;) {
		my $block = wait_for_block ($fh, "wait_for_spm");
		my $obj = $json->jsonToObj($block);
		if ($obj->{PGM}->{type} =~ /SPM/) {
			print "spm received: ";
			print $json->objToJson($obj) . "\n";
			return $obj;
		}
	}
}

sub wait_for_odata {
	my $fh = shift;
	my $json = new JSON;

	for (;;) {
		my $block = wait_for_block ($fh, "wait_for_odata");
		my $obj = $json->jsonToObj($block);
		if ($obj->{PGM}->{type} =~ /ODATA/) {
			print "odata received: ";
			print $json->objToJson($obj) . "\n";
			return $obj;
		}
	}
}

sub wait_for_rdata {
	my $fh = shift;
	my $json = new JSON;

	for (;;) {
		my $block = wait_for_block ($fh, "wait_for_rdata");
		my $obj = $json->jsonToObj($block);
		if ($obj->{PGM}->{type} =~ /RDATA/) {
			print "rdata received: ";
			print $json->objToJson($obj) . "\n";
			return $obj;
		}
	}
}

print "mon: wait for odata ...\n";
my $odata = "";
for (my $i = 1; $i <= 3; $i++)
{
# tail monitor for odata, newline is required on die.
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };

		alarm 10;
		$odata = wait_for_odata(\*MON_READER);
		alarm 0;
	};
	if ($@) {
		close_ssh();
		die unless $@ eq "alarm\n";
		die "alarm terminated test.\n";
	}
}

print "mon: received 3 x odata\n";

# tell sim to publish nak #2

print "sim: net send nak baa " . $odata->{PGM}->{gsi} . "." . $odata->{PGM}->{sourcePort} . " 2\n";
print SIM_WRITER "net send nak baa " . $odata->{PGM}->{gsi} . "." . $odata->{PGM}->{sourcePort} . " 2\n";
wait_for_ready (\*SIM_READER, "sim");

# tail monitor for rdata

eval {
	local $SIG{ALRM} = sub { die "alarm\n" };

	alarm 10;
	wait_for_rdata(\*MON_READER);
	alarm 0;
};
if ($@) {
	print "alarm terminated test, flushing output.\n" if $@ eq "alarm\n";
	flush_ssh();
}

print "test completed successfully, terminating.\n";


# cleanup
sub flush_ssh {
	print APP_WRITER "quit\n";
	print SIM_WRITER "quit\n";
	while (<APP_READER>) { print "app: $_"; }
	print "app terminated.\n";
	while (<SIM_READER>) { print "sim: $_"; }
	print "sim terminated.\n";

# ignore monitor output, usually excess SPMs
	print MON_WRITER "quit\n";
	while (<MON_READER>) {}
	print "mon terminated.\n";
	close_ssh();
}

flush_ssh();

# eof

#!/usr/bin/perl

use Net::SSH qw(sshopen2);
use POSIX ":sys_wait_h";
use JSON;

use strict;

my $app_host = 'ayaka';
my $app_ip = '10.6.28.31';
my $mon_host = 'hikari';
my $mon = '/miru/projects/openpgm/pgm/ref/debug/test/monitor';
my $app = '/miru/projects/openpgm/pgm/ref/debug/test/app';


$| = 1;

# setup reaper for failed SSH connections

my %Kid_Status;

sub REAPER {
	my $child;
	while (($child = waitpid(-1,WNOHANG)) > 0) {
		$Kid_Status{$child} = $?;
	}
	$SIG{CHLD} = \&REAPER;
}
$SIG{CHLD} = \&REAPER;


# start monitor

my $host = $mon_host;
my $cmd = "uname -a && sudo $mon";

sshopen2 ($host, *MON_READER, *MON_WRITER, $cmd) || die "ssh: $!";


# start sim
# start app

$host = $app_host;
$cmd = "uname -a && sudo $app";

sshopen2 ($host, *APP_READER, *APP_WRITER, $cmd) || die "ssh: $!";


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
print "monitor ready.\n";

wait_for_ready (\*APP_READER, "app");
print APP_WRITER "create moo\n";
wait_for_ready (\*APP_READER, "app");
print APP_WRITER "bind moo\n";
wait_for_ready (\*APP_READER, "app");
print "app ready.\n";

# tell app to publish odata

print APP_WRITER "send moo ringo\n";

sub wait_for_block {
	my($fh) = @_;
	my $b = '';
	my $state = 0;

#	print "wait_for_block ...\n";
	while (<$fh>) {
		chomp();
		my $l = $_;
		if ($state == 0) {
			if ($l =~ /^{$/) {
				$state = 1;
			} else {
				print "$l\n";
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

sub wait_for_odata {
	my $fh = shift;
	my $json = new JSON;

	print "wait_for_odata ...\n";
	for (;;) {
		my $block = wait_for_block ($fh);
#		print "block returned, parsing ...\n";
		my $obj = $json->jsonToObj($block);
		print "packet: " . $obj->{PGM}->{type} . "\n";
		if ($obj->{PGM}->{type} =~ /ODATA/) {
			return;
		}
	}
}

# tail monitor for odata 

eval {
	local $SIG{ALRM} = sub { die "alarm\n" };

	alarm 10;
#while (<MON_READER>) { print; }
	wait_for_odata(\*MON_READER);
	alarm 0;
};
if ($@) {
	die unless $@ eq "alarm\n";
	die "alarm terminated test.\n";
}

# tell sim to publish nak list #1, #2, #3
# tail monitor for rdata

# cleanup
print APP_WRITER "quit\n";
print MON_WRITER "quit\n";
while (<APP_READER>) {
	chomp;
	print "app: $_\n";
}
print "app terminated.\n";
close(APP_READER);
close(APP_WRITER);
while (<MON_READER>) {
	chomp;
	print "mon: $_\n";
}
print "mon terminated.\n";
close(MON_READER);
close(MON_WRITER);

# eof

#!/usr/bin/perl
# rdata_completion_parity_var_pktlen.pl
# 6.3. Data Recovery by Negative Acknowledgment

use strict;
use Time::HiRes qw( gettimeofday tv_interval );
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

$app->say ("create ao");
$app->say ("set ao FEC RS(255,4)");
$app->say ("bind ao");
$app->say ("listen ao");

## capture GSI of test spp
$app->say ("send ao nashi");
print "mon: wait for odata ...\n";
my $odata = $mon->wait_for_odata;
print "mon: odata received.\n";

$sim->say ("create fake ao");
$sim->say ("set ao FEC RS(255,4)");
$sim->say ("bind ao");

print "sim: publish SPM txw_trail 32,769 txw_lead 32,768 at spm_sqn 3200, advertise on-demand parity, k=4.\n";
$sim->say ("net send spm ao 3200 32768 32767 on-demand 4");

# no NAKs should be generated.
print "sim: waiting 2 seconds for erroneous NAKs ...\n";
$sim->die_on_nak ({ 'timeout' => 2 });
print "sim: no NAKs received.\n";

print "sim: publish ODATA sqn 32,768.\n";
$sim->say ("net send odata ao 32768 32768 ringo");

print "app: wait for data ...\n";
my $data = $app->wait_for_data;
print "app: data received [$data].\n";

# no NAKs should be generated.
print "sim: waiting 2 seconds for erroneous NAKs ...\n";
$sim->die_on_nak ({ 'timeout' => 2 });
print "sim: no NAKs received.\n";

print "sim: publish ODATA sqn 32,770.\n";
$sim->say ("net send odata ao 32770 32768 momo");
print "sim: publish ODATA sqn 32,771.\n";
$sim->say ("net send odata ao 32771 32768 yakitori");
print "sim: publish ODATA sqn 32,772.\n";
$sim->say ("net send odata ao 32772 32768 sasami");
print "sim: publish ODATA sqn 32,773.\n";
$sim->say ("net send odata ao 32773 32768 tebasaki");

print "sim: waiting for valid parity NAK.\n";
my $nak = $sim->wait_for_nak;
die "Selective NAK received, parityPacket=false\n" unless $nak->{PGM}->{options}->{parityPacket};
print "sim: Parity NAK received.\n";

print "sim: publish parity RDATA, tg_sqn 32,768, pkt_cnt 1 (sqn 32,768).\n";
$sim->say ("net send parity rdata ao 32768 32768 ringo ichigo momo yakitori");

for (1..5)
{
	print "app: wait for data ...\n";
	my $data = $app->wait_for_data;
	print "app: data received [$data].\n";
}

print "test completed successfully.\n";

$mon->disconnect (1);
$sim->disconnect;
$app->disconnect;
close_ssh;

# eof

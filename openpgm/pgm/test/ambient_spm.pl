#!/usr/bin/perl
# ambient_spm.pl
# 5.1.4. Ambient SPMs

use strict;
use PGM::Test;
use IO::Handle;

BEGIN { require "test.conf.pl"; }

$| = 1;

my $mon = PGM::Test->new(tag => 'mon', host => $config{mon}{host}, cmd => $config{mon}{cmd});
my $app = PGM::Test->new(tag => 'app', host => $config{app}{host}, cmd => $config{app}{cmd});

pipe(FROM_PARENT, TO_CHILD) or die "pipe: $!";
FROM_PARENT->autoflush(1);

$mon->connect;
$app->connect;

sub close_ssh {
	close FROM_PARENT; close TO_CHILD;
	$mon = $app = undef;
	print "finished.\n";
}

$SIG{'INT'} = sub { print "interrupt caught.\n"; close_ssh(); };

$mon->say ("filter $config{app}{ip}");
print "mon: ready.\n";

$app->say ("create ao");
$app->say ("bind ao");
$app->say ("connect ao");
$app->say ("listen ao");

if (my $pid = fork) {
# parent 
	close FROM_PARENT;

	print "mon: wait for odata ...\n";
	$mon->wait_for_odata;
	print "mon: odata received.\n";
	print "mon: wait for spm ...\n";
	$mon->wait_for_spm ({ 'timeout' => 45 });
	print "mon: received spm.\n";

	print TO_CHILD "die\n";

	close TO_CHILD;
	waitpid($pid,0);
} else {
# child
	die "cannot fork: $!" unless defined $pid;
	close TO_CHILD;
	print "app: loop sending data.\n";
	vec(my $rin, fileno(FROM_PARENT), 1) = 1;
	my $rout = undef;

# hide stdout
	open(OLDOUT, ">&STDOUT");
	open(STDOUT, ">/dev/null") or die "Can't redirect stdout: $!";

# send every ~50ms
	while (! select($rout = $rin, undef, undef, 0.05))
	{
		$app->say ("send ao ringo");
	}

# restore stdout
	close(STDOUT) or die "Can't close STDOUT: $!";
	open(STDOUT, ">&OLDOUT") or die "Can't restore stdout: $!";
	close(OLDOUT) or die "Can't close OLDOUT: $!";

	print "app: loop finished.\n";
	close FROM_PARENT;
	exit;
}

print "test completed successfully.\n";

$mon->disconnect (1);
$app->disconnect;
close_ssh;

# eof

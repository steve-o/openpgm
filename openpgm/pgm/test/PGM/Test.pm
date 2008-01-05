package PGM::Test;

use strict;
our($VERSION);
use Carp;
use IO::File;
use Net::SSH qw(sshopen2);
use POSIX ":sys_wait_h";
use JSON;

$VERSION = '1.00';

=head1 NAME

PGM::Test - PGM test module

=head1 SYNOPSIS

  $test = PGM::Test->new();

=cut

my $json = new JSON;

sub new {
	my $class = shift;
	my $self = {};
	my %params = @_;

	$self->{tag} = exists $params{tag} ? $params{tag} : confess "tag parameter is required";
	$self->{host} = exists $params{host} ? $params{host} : confess "host parameter is required";
	$self->{cmd} = exists $params{cmd} ? $params{cmd} : confess "cmd parameter is required";

	$self->{in}  = IO::File->new();
	$self->{out} = IO::File->new();
	$self->{pid} = undef;

	bless $self, $class;
	return $self;
}

sub connect {
	my $self = shift;

	print "$self->{tag}: opening SSH connection to $self->{host} ...\n";
	$self->{pid} = sshopen2 ($self->{host},
				 $self->{in},
				 $self->{out},
				 "uname -a && sudo $self->{cmd}")
		or croak "SSH failed: $!";
	print "$self->{tag}: connected.\n";
	$self->wait_for_ready;
}

sub disconnect {
	my($self,$quiet) = @_;
	my $out = $self->{out};

	print "$self->{tag}: sending quit command ...\n";
	eval {
		local($SIG{ALRM}) = sub { die "alarm\n"; };
		alarm 10;
		print $out "quit\n";
		while (readline($self->{in})) {
			chomp;
			print "$self->{tag} [$_]\n" if (!$quiet);
		}
		alarm 0;
	};
	if ($@) {
		print "$self->{tag}: alarm raised on quit command.\n";
	} else {
		print "$self->{tag}: eof.\n";
	}

	print "$self->{tag}: closing SSH connection ...\n";
	close ($self->{in});
	close ($self->{out});
	print "$self->{tag}: closed.\n";
}

sub DESTROY {
	my $self = shift;

	if ($self->{pid}) {
		print "$self->{tag}: waiting child to terminate ...\n";
		eval {
			local($SIG{ALRM}) = sub { die "alarm\n"; };
			alarm 10;
			waitpid $self->{pid}, 0;
			alarm 0;
		};
		if ($@) {
			die unless $@ eq "alarm\n";
			local($SIG{CHLD}) = 'IGNORE';
			print "$self->{tag}: killing SSH connection ...\n";
			kill 'INT' => $self->{pid};
			print "$self->{tag}: killed.\n";
		} else {
			print "$self->{tag}: terminated.\n";
		}
	}
}

sub wait_for_ready {
	my $self = shift;

	while (readline($self->{in})) {
		chomp;
		print "$self->{tag} [$_]\n";
		last if /^READY/;
	}
}

sub wait_for_block {
	my $self = shift;
	my $fh = $self->{in};
	my $b = '';
	my $state = 0;

	while (<$fh>) {
		chomp();
		my $l = $_;
		if ($state == 0) {
			if ($l =~ /^{$/) {
				$state = 1;
			} else {
				print "$self->{tag} [$l]\n";
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
	my $self = shift;
	my $timeout = ref($_[0]) ? $_[0]->{'timeout'} : 10;
	my $obj = undef;

	eval {
		local $SIG{ALRM} = sub { die "alarm\n"; };
		alarm $timeout;
		for (;;) {
			my $block = $self->wait_for_block;
			$obj = $json->jsonToObj($block);
			last if ($obj->{PGM}->{type} =~ /SPM/);
		}
		alarm 0;
	};
	if ($@) {
		die unless $@ eq "alarm\n";
		confess "$self->{tag}: alarm raised waiting for spm.\n";
	}

	return $obj;
}

sub wait_for_odata {
	my $self = shift;
	my $timeout = ref($_[0]) ? $_[0]->{'timeout'} : 10;
	my $obj = undef;

	eval {
		local $SIG{ALRM} = sub { die "alarm\n"; };
		alarm $timeout;
		for (;;) {
			my $block = $self->wait_for_block;
			$obj = $json->jsonToObj($block);
			last if ($obj->{PGM}->{type} =~ /ODATA/);
		}
		alarm 0;
	};
	if ($@) {
		die unless $@ eq "alarm\n";
		confess "$self->{tag}: alarm raised waiting for odata.\n";
	}

	return $obj;
}

sub wait_for_rdata {
	my $self = shift;
	my $timeout = ref($_[0]) ? $_[0]->{'timeout'} : 10;
	my $obj = undef;

	eval {
		local $SIG{ALRM} = sub { die "alarm\n"; };
		alarm $timeout;
		for (;;) {
			my $block = $self->wait_for_block;
			$obj = $json->jsonToObj($block);
			last if ($obj->{PGM}->{type} =~ /RDATA/);
		}
		alarm 0;
	};
	if ($@) {
		die unless $@ eq "alarm\n";
		confess "$self->{tag}: alarm raised waiting for odata.\n";
	}

	return $obj;
}

sub wait_for_ncf {
	my $self = shift;
	my $timeout = ref($_[0]) ? $_[0]->{'timeout'} : 10;
	my $obj = undef;

	eval {
		local $SIG{ALRM} = sub { die "alarm\n"; };
		alarm $timeout;
		for (;;) {
			my $block = $self->wait_for_block;
			$obj = $json->jsonToObj($block);
			last if ($obj->{PGM}->{type} =~ /NCF/);
		}
		alarm 0;
	};
	if ($@) {
		die unless $@ eq "alarm\n";
		confess "$self->{tag}: alarm raised waiting for ncf.\n";
	}

	return $obj;
}

sub print {
	my $self = shift;
	my $timeout = ref($_[0]) ? $_[0]->{'timeout'} : 10;
	my $out = $self->{out};

	print "$self->{tag}> @_";
	eval {
		local($SIG{ALRM}) = sub { die "alarm\n"; };
		alarm $timeout;
		print $out "@_";
		$self->wait_for_ready;
		alarm 0;
	};
	if ($@) {
		die unless $@ eq "alarm\n";
		confess "$self->{tag}: alarm raised.\n";
	}
}

sub say {
	my $self = shift;
	$self->print ("@_\n");
}

1;

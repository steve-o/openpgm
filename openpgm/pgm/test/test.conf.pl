# test.conf.pl
use vars qw ( %config );

%config = (
	app => {
		host	=> 'ayaka',
		ip	=> '10.6.28.31',
		cmd	=> '/miru/projects/openpgm/pgm/ref/debug/test/app',
	},
	mon => {
		host	=> 'kiku',
		cmd	=> '/miru/projects/openpgm/pgm/ref/debug/test/monitor',
	},
	sim => {
		host	=> 'kiku',
		cmd	=> '/miru/projects/openpgm/pgm/ref/debug/test/sim',
	},
);

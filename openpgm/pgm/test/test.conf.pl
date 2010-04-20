# test.conf.pl
use vars qw ( %config );

%config = (
	msapp => {
		host	=> 'momo',
		ip	=> '10.6.28.34',
	},
	app => {
		host	=> 'ayaka',	ip	=> '10.6.28.31',
		cmd	=> '/miru/projects/openpgm/pgm/ref/debug/test/app',
#		host	=> 'ryoko',	ip	=> '10.6.28.36',
#		cmd	=> 'LD_LIBRARY_PATH=/opt/glib-sunstudio/lib:$LD_LIBRARY_PATH /miru/projects/openpgm/pgm/ref/debug/test/app',
	},
	mon => {
		host	=> 'sora',
		cmd	=> '/miru/projects/openpgm/pgm/ref/release/test/monitor',
	},
	sim => {
		host	=> 'kiku',
		cmd	=> '/miru/projects/openpgm/pgm/ref/release/test/sim',
	},
);

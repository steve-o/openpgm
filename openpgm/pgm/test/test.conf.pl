# test.conf.pl
use vars qw ( %config );

%config = (
	msapp => {
		host	=> 'momo',
		ip	=> '10.6.28.34',
	},
	app => {
#		host	=> 'ayaka',	ip	=> '10.6.28.31',
#		cmd	=> '/miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/app',
#		network => 'eth0;239.192.0.1'
		host	=> 'ryoko',	ip	=> '10.6.28.36',
		cmd	=> 'LD_LIBRARY_PATH=/opt/glib-sunstudio/lib:$LD_LIBRARY_PATH /miru/projects/openpgm/pgm/ref/release-SunOS-sun4u-sunstudio/test/app',
		network => 'eri0;239.192.0.1'
	},
	mon => {
		host	=> 'sora',
		cmd	=> '/miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/monitor',
		network => 'eth0;239.192.0.1'
	},
	sim => {
		host	=> 'kiku',
		cmd	=> '/miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/sim',
		network => 'eth0;239.192.0.1'
	},
);

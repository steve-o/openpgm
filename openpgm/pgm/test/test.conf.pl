# test.conf.pl
use vars qw ( %config );

%config = (
	app => {
# Linux host
#		host	=> 'ayaka',
#		ip	=> '10.6.28.31',
#		cmd	=> 'sudo /miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/app',
#		network => 'eth0;239.192.0.1'
# Solaris host
#		host	=> 'ryoko',
#		ip	=> '10.6.28.36',
#		cmd	=> 'sudo LD_LIBRARY_PATH=/opt/glib-sunstudio/lib:$LD_LIBRARY_PATH /miru/projects/openpgm/pgm/ref/release-SunOS-sun4u-sunstudio/test/app',
#		network => 'eri0;239.192.0.1'
# Windows host
		host	=> 'Steve-o@hinano',
		ip	=> '10.6.28.102',
		cmd	=> '/cygdrive/c/temp/app.exe',
		network	=> '10.6.28.102;239.192.0.1'
	},
	mon => {
		host	=> 'sora',
		cmd	=> 'sudo /miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/monitor',
		network => 'eth0;239.192.0.1'
	},
	sim => {
		host	=> 'kiku',
		cmd	=> 'sudo /miru/projects/openpgm/pgm/ref/release-Linux-x86_64/test/sim',
		network => 'eth0;239.192.0.1'
	},
);

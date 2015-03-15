#summary OpenPGM 2 : C Reference : Build Library : GCC 3 on OpenSolaris 2009.06
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC for OpenSolaris 2009.06 ###
First install all the C compiler dependencies and Subversion.
<pre>
SUNWsvn SUNWgcc<br>
</pre>
Download and build stable release of SCons.
<pre>
$ *wget http://nchc.dl.sourceforge.net/sourceforge/scons/scons-1.2.0.tar.gz *<br>
$ *tar zxf scons-1.2.0.tar.gz*<br>
$ *cd scons-1.2.0*<br>
$ *sudo python setup.py install*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout -r 956  http://openpgm.googlecode.com/svn/trunk *<br>
</pre>
Build.
<pre>
$ *cd trunk/openpgm/pgm*<br>
$ *scons -f SConstruct.!OpenSolaris*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-OpenSolaris-i686pc-gcc</tt>:

To build the release version in <tt>./ref/release-OpenSolaris-i686pc-gcc</tt> use the following:
<pre>
$ *scons -f SConstruct.!OpenSolaris BUILD=release*<br>
</pre>

#### Testing ####
Testing must be performed with the pure examples unless GLib is installed.  Default configuration will use the TSC and it is recommended to set the environment variable <tt>RDTSC_FREQUENCY</tt> to accelerate startup.
<pre>
$ *RDTSC_FREQUENCY=2800 \*<br>
*./ref/debug-!OpenSolaris-i686pc-gcc/examples/purinrecv \*<br>
*-lp 7500 -n "e1000g0;239.192.0.1"*<br>
プリン プリン<br>
Create transport.<br>
Startup complete.<br>
Entering PGM message loop ...<br>
</pre>
Then send a few messages, for example from same host using loopback,
<pre>
$ *RDTSC_FREQUENCY=2800 \*<br>
*./ref/debug-!OpenSolaris-i686pc-gcc/examples/purinsend \*<br>
*-lp 7500 -n "e1000g0;239.192.0.1" \*<br>
*purin purin*<br>
</pre>
And witness the receiver processing the incoming messages.
<pre>
"purin" (6 bytes from 106.206.245.222.65.119.43353)<br>
"purin" (6 bytes from 106.206.245.222.65.119.43353)<br>
</pre>
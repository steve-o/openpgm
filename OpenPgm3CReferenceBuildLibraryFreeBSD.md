#summary OpenPGM 3 : C Reference : Build Library : GCC 4 on FreeBSD 8.0
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC for FreeBSD 8.0 ###
First install SCons and Subversion, you may wish to install <tt>sudo</tt> for convenience first.
<pre>
$ *sudo pkg_add -r scons*<br>
$ *sudo pkg_add -r subversion*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout -r 948 http://openpgm.googlecode.com/svn/trunk *<br>
</pre>
Build.
<pre>
$ *cd trunk/openpgm/pgm*<br>
$ *scons -f SConstruct.FreeBSD80*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-FreeBSD-amd64</tt> or <tt>./ref/debug-FreeBSD-i686</tt> dependent upon your machine.

To build the release version in <tt>./ref/release-FreeBSD-amd64</tt> use the following:
<pre>
$ *scons -f SConstruct.FreeBSD80 BUILD=release*<br>
</pre>

#### Testing ####
Testing must be performed with the pure examples unless GLib is installed.  Default configuration will use the TSC and it is recommended to set the environment variable <tt>RDTSC_FREQUENCY</tt> to accelerate startup.
<pre>
$ *RDTSC_FREQUENCY=3200 ./ref/debug-FreeBSD-amd64/examples/purinrecv -lp 7500 -n "bge0;239.192.0.1"*<br>
プリン プリン<br>
Create transport.<br>
Startup complete.<br>
Entering PGM message loop ...<br>
</pre>
Then send a few messages, for example from a Linux host,
<pre>
$ *./ref/debug-Linux-x86_64/examples/purinsend  -lp 7500 -n "eth0;239.192.0.1" purin purin*<br>
</pre>
And witness the receiver processing the incoming messages.
<pre>
"purin" (6 bytes from 170.29.65.155.237.36.57140)<br>
"purin" (6 bytes from 170.29.65.155.237.36.57140)<br>
</pre>
#summary OpenPGM 3 : C Reference : Build Library : GCC 4 on OS X 10.6
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC for OS X 10.6 ###
Download and build stable release of SCons, you may need to set a password before using <tt>sudo</tt>.
<pre>
$ *wget http://nchc.dl.sourceforge.net/sourceforge/scons/scons-1.2.0.tar.gz *<br>
$ *tar zxf scons-1.2.0.tar.gz*<br>
$ *cd scons-1.2.0*<br>
$ *sudo python setup.py install*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout http://openpgm.googlecode.com/svn/tags/release-3-0-66/ *<br>
</pre>
Build.
<pre>
$ *cd trunk/openpgm/pgm*<br>
$ *scons -f SConstruct.OSX106*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-Darwin-i386</tt>, noting the architecture name.

To build the release version in <tt>./ref/release-Darwin-i386</tt> use the following:
<pre>
$ *scons -f SConstruct.OSX106 BUILD=release*<br>
</pre>

#### Testing ####
Testing must be performed with the pure examples unless GLib is installed.
<pre>
$ *./ref/debug-Darwin-i386/examples/purinrecv -lp 7500 -n "en0;239.192.0.1"*<br>
プリン プリン<br>
Create transport.<br>
Startup complete.<br>
Entering PGM message loop ...<br>
</pre>
Then send a few messages, for example from the same host,
<pre>
$ *./ref/debug-Darwin-i386/examples/purinsend  -lp 7500 -n "en0;239.192.0.1" purin purin*<br>
</pre>
And witness the receiver processing the incoming messages.
<pre>
"purin" (6 bytes from 170.29.65.155.237.36.57140)<br>
"purin" (6 bytes from 170.29.65.155.237.36.57140)<br>
</pre>
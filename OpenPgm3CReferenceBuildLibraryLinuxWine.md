#summary OpenPGM 2 : C Reference : Build Library : GCC 4 for Wine
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC for 32-bit Wine on Ubuntu ###
Microsoft Windows is supported by cross-compiling on Ubuntu 9.04 through 10.04 with MinGW.  Special options are required to limit the API usage in order for OpenPGM to function on [Wine](http://www.winehq.org/).

First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ *sudo apt-get install build-essential scons subversion mingw32 wine1.2*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout -r 952 http://openpgm.googlecode.com/svn/trunk *<br>
$ *cd trunk/openpgm/pgm*<br>
</pre>
Patch the mingw32 installation with updated Win32 headers.  For Ubuntu 9.04 or 9.10 use the 3.13 runtime patch,
<pre>
$ *sudo patch -d /usr/i586-mingw32msvc/include < mingw32-runtime_3.13-1openpgm3.diff*<br>
</pre>
For Ubuntu 10.04 use the 3.15 runtime patch,
<pre>
$ *sudo patch -d /usr/i586-mingw32msvc/include < mingw32-runtime_3.15.2-0openpgm1.diff*<br>
</pre>
Build.
<pre>
$ *cd ..*<br>
$ *scons -f SConstruct.mingw-wine*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-Wine-i686</tt>:

To build the release version in <tt>./ref/release-Wine-i686</tt> use the following:
<pre>
$ *scons -f SConstruct.mingw-wine BUILD=release*<br>
</pre>

#### Testing ####
Testing must be performed with the pure examples unless GLib is installed.  Unicode output may not be displayed correctly depending upon your system settings.
<pre>
$ *./ref/debug-Wine-i686/examples/purinrecv.exe -lp 7500 -n "eth0;239.192.0.1"*<br>
プリン プリン<br>
Create transport.<br>
fixme:winsock:convert_aiflag_w2u Unhandled windows AI_xxx flags 20<br>
fixme:winsock:convert_aiflag_w2u Unhandled windows AI_xxx flags 20<br>
Startup complete.<br>
Entering PGM message loop ...<br>
</pre>
Then send a few messages, for example from the same host, native, via loopback
<pre>
$ *./ref/debug-Linux-i686/examples/purinsend -lp 7500 -n "eth0;239.192.0.1" purin purin*<br>
</pre>
And witness the receiver processing the incoming messages.
<pre>
"purin" (6 bytes from 76.139.60.230.224.49.47737)<br>
"purin" (6 bytes from 76.139.60.230.224.49.47737)<br>
</pre>
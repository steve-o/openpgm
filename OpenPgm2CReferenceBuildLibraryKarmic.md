#summary OpenPGM 2 : C Reference : Build Library : Ubuntu 9.04 through 10.04
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building for Ubuntu 9.04 through 10.04 ###
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ sudo apt-get install build-essential scons subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.4-dev libncurses5-dev libsnmp-dev<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ scons<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons BUILD=release<br>
</pre>
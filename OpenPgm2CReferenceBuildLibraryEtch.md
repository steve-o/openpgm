#summary OpenPGM 2 : C Reference : Build Library : Debian 4.0
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building on Debian 4.0 ###
Requires new installation of SCons as repository only contains version 0.96 which does not support <tt>autoconf</tt>.

First install all the C compiler dependencies and Subversion.
<pre>
$ sudo apt-get install build-essential subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.4-dev libncurses5-dev libsnmp-dev<br>
</pre>
Install dependencies to build a Debian package.
<pre>
# sudo apt-get install fakeroot dpkg-dev<br>
</pre>
Pull in the 1.0.0 SCons source from Lenny.
<pre>
$ wget http://ftp.de.debian.org/debian/pool/main/s/scons/scons_1.0.0-1.dsc<br>
$ wget http://ftp.de.debian.org/debian/pool/main/s/scons/scons_1.0.0.orig.tar.gz<br>
$ wget http://ftp.de.debian.org/debian/pool/main/s/scons/scons_1.0.0-1.diff.gz<br>
$ dpkg-source -x scons_1.0.0-1.dsc<br>
$ cd scons-1.0.0/<br>
$ sudo apt-get build-dep scons<br>
$ dpkg-buildpackage -rfakeroot -b<br>
</pre>
Install the new package.
<pre>
$ sudo dpkg -i ../scons_1.0.0-1_all.deb<br>
$ cd ..<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ scons -f SConstruct.Debian4<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.Debian4<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.Debian4 BUILD=release<br>
</pre>
#summary OpenPGM : C Reference : Build Library
#labels Phase-Implementation
#sidebar TOCCReference
### Introduction ###
There are currently no official releases or pre-built packages in any distributions.  The OpenPGM library must be built from source taken from the projects subversion repository.

## Building in openSUSE 11.1 ##
First install the C compiler dependencies and the SCons build tool.
<pre>
$ sudo zypper install gcc scons<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.
<pre>
$ sudo zypper install libsoup-devel net-snmp-devel<br>
</pre>
To download the source code Subversion is needed to contact the repository.
<pre>
$ sudo zypper install subversion<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build without the ncurses examples as <tt>libpanel</tt> is not included.
<pre>
$ cd trunk/openpgm/pgm<br>
$ scons WITH_NCURSES=false<br>
</pre>
By default SCons is configured to build the debug tree in <tt>./ref/debug</tt> to build the release version in <tt>./ref/release</tt>.
<pre>
$ scons BUILD=release WITH_NCURSES=false<br>
</pre>

## Building in OpenSolaris 2008.11 under SCons ##
First install all the C compiler dependencies and Subversion.  Install the library dependencies, primarily GLib.
<pre>
SUNWsvn SUNWgcc SUNWgnome-common-devel SUNWgnome-base-libs SUNWlibsoup (IPSnetsnmp)<br>
</pre>
Download and build stable release of SCons.
<pre>
$ wget http://nchc.dl.sourceforge.net/sourceforge/scons/scons-1.2.0.tar.gz<br>
$ tar zxf scons-1.2.0.tar.gz<br>
$ cd scons-1.2.0<br>
$ sudo python setup.py install<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
$ scons -f SConstruct.Solaris<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.Solaris<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.Solaris BUILD=release<br>
</pre>

## Building in Ubuntu 9.04 under SCons ##
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
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
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

## Building in Ubuntu 8.04 or 8.10 under SCons ##
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ sudo apt-get install build-essential scons subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.2-dev libncurses5-dev libsnmp-dev<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
$ scons<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.097<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.097 BUILD=release<br>
</pre>

## Building in Ubuntu 8.04 or 8.10 under CMake ##
First install all the C compiler dependencies, CMake, and Subversion.
<pre>
$ sudo apt-get install build-essential cmake subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API, libsoup-2.4 is not supported.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.2-dev libncurses5-dev libsnmp-dev<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
$ mkdir build<br>
$ cd build<br>
</pre>
By default CMake is configured to build a debug tree in the current directory:
<pre>
$ cmake ..<br>
$ make<br>
</pre>
To build a release version in <tt>./build/release</tt> use the following:
<pre>
$ cd trunk/openpgm/pgm<br>
$ mkdir -p build/release<br>
$ cd build/release<br>
$ cmake -DCMAKE_BUILD_TYPE=!RelWithDebInfo ../..<br>
$ make<br>
</pre>

## Building in Debian 4.0 under SCons ##
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
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
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

## Building in Debian 4.0 under CMake ##
First install all the C compiler dependencies, CMake, and Subversion.
<pre>
$ sudo apt-get install build-essential cmake subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API, libsoup-2.4 is not supported.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.2-dev libncurses5-dev libsnmp-dev<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Locate the <tt>FindPkgConfig</tt> module missing from the Etch patch revision.
<pre>
$ wget http://ftp.de.debian.org/debian/pool/main/c/cmake/cmake_2.6.0.orig.tar.gz<br>
$ tar xzOf cmake_2.6.0.orig.tar.gz cmake-2.6.0/Modules/FindPkgConfig.cmake > CMake/FindPkgConfig.cmake<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
$ mkdir build<br>
$ cd build<br>
</pre>
By default CMake is configured to build a debug tree in the current directory:
<pre>
$ cmake ..<br>
$ make<br>
</pre>
To build a release version in <tt>./build/release</tt> use the following:
<pre>
$ cd trunk/openpgm/pgm<br>
$ mkdir -p build/release<br>
$ cd build/release<br>
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ../..<br>
$ make<br>
</pre>

## Building in RHEL or CentOS 4.x ##
Don't, for the following reasons.

### Issues ###
  1. <tt>pkg-config</tt> does not support <tt>--static</tt> to provide static library parameters for GLib and other packages.
  1. <tt>libsoup-devel-2.2.1-4.i386.rpm</tt> is broken.
  1. Old <tt>gcc</tt> does not support new warnings, <tt>-Wunsafe-loop-optimizations</tt>.
  1. GLib 2.10 is required for the [SLAB allocator](http://library.gnome.org/devel/glib/stable/glib-Memory-Slices.html), the default packaged version is 2.4.  A special 2.12 build is available as a dependency for Evolution, <tt>evolution28-glib2-devel.i386</tt>.
  1. <tt>libsoup</tt> 2.2.1 does not support asynchronous callbacks, however the <tt>evolution28-libsoup-devel.i386</tt> does.
  1. <tt>evolution28-libsoup-devel.i386</tt> has an extra hidden dependency on <tt>gobject</tt>.


### Rebuilding yum packages ###
Setup user packaging as per [YumAndRPM](http://wiki.centos.org/TipsAndTricks/YumAndRPM) in the CentOS wiki.  Download the source package from Red Hat or CentOS, then rebuild the package, installing any additional build dependencies as necessary.
<pre>
$ wget http://mirror.centos.org/centos/4/os/SRPMS/libsoup-2.2.1-4.src.rpm<br>
$ sudo yum install gnutls-devel<br>
$ rpmbuild --rebuild libsoup-*.src.rpm<br>
$ sudo rpm -i redat/RPMS/libsoup-devel-*.rpm<br>
</pre>

### Building ###
First install the C compiler dependencies.
<pre>
$ sudo yum install gcc<br>
</pre>
Install SCons by downloading the [0.97 stable package](http://scons.org/download.php).
<pre>
$ sudo rpm -i scons-0.97-1.noarch.rpm<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note we are install the Evolution 2.8 versions of the GLib suite, not the default packages.
<pre>
$ sudo yum install evolution28-glib2-devel evolution28-libsoup-devel ncurses-devel net-snmp-devel<br>
</pre>
Red Hat net-snmp libraries are a bit chunky and need some extras:
<pre>
$ sudo yum install lm_sensors-devel<br>
</pre>
To download the source code Subversion is needed to contact the repository.
<pre>
$ sudo yum install subversion<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
For the default debug build use the following and look in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.RHEL4<br>
</pre>

### Testing ###
The lack of static library support in <tt>pkg-config</tt> requires extra steps for testing, Red Hat <tt>sudo</tt> by default blocks <tt>LD_LIBRARY_PATH</tt> exports.
<pre>
$ sudo bash<br>
$ LD_LIBRARY_PATH=/usr/evolution28/lib ./pgmrecv<br>
</pre>

## Building in RHEL or CentOS 5.1-5.3 ##
First install the C compiler dependencies.
<pre>
$ sudo yum install gcc<br>
</pre>
Install SCons by downloading the [0.97 stable package](http://scons.org/download.php).
<pre>
$ sudo rpm -i scons-0.97-1.noarch.rpm<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.
<pre>
$ sudo yum install glib2-devel libsoup-devel ncurses-devel net-snmp-devel<br>
</pre>
Red Hat net-snmp libraries are a bit chunky and need some extras:
<pre>
$ sudo yum install lm_sensors-devel<br>
</pre>
To download the source code Subversion is needed to contact the repository.
<pre>
$ sudo yum install subversion<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/tags/release-1-0-0<br>
</pre>
Build.
<pre>
$ cd trunk/openpgm/pgm<br>
$ scons -f SConstruct.097<br>
</pre>
By default SCons is configured to build the debug tree in <tt>./ref/debug</tt> to build the release version in <tt>./ref/release</tt>.
<pre>
$ scons -f SConstruct.097 BUILD=release<br>
</pre>

## PGM Testing ##
Two hosts are required for full PGM protocol testing, one to send the other to receive.  In this example <tt>ayaka</tt> is the sending host and <tt>kiku</tt> is receiving.

On the receiving host run the OpenPGM receiver.
<pre>
kiku$ sudo ./ref/debug/examples/pgmrecv<br>
** Message: pgmrecv<br>
2008-05-27 18:02:53 kiku: scheduling startup.<br>
2008-05-27 18:02:53 kiku: entering main event loop ...<br>
2008-05-27 18:02:53 kiku: startup.<br>
2008-05-27 18:02:53 kiku: create transport.<br>
2008-05-27 18:02:53 kiku: startup complete.<br>
</pre>
On the sending host run the OpenPGM publisher.
<pre>
ayaka$ sudo ./ref/debug/examples/pgmsend mooooo baa<br>
</pre>
Then on the receiver you should see the test messages.
<pre>
2008-05-27 18:03:25 kiku: (7 bytes)<br>
2008-05-27 18:03:25 kiku: 	1: "mooooo" (7 bytes)<br>
2008-05-27 18:03:25 kiku: (4 bytes)<br>
2008-05-27 18:03:25 kiku: 	1: "baa" (4 bytes)<br>
</pre>
Without explicit network parameter passing OpenPGM will assume the default adapter and either IPv4 or IPv6 addressing.  If your operating system defaults to DHCP it is possible that the nodename of the host resolves to localhost and not a real adapter, it is then necessary to explicitly set the adapter name.  Similarly you might have problems with IPv6 auto-configuration and multiple scopes per adapter.  So on a default OpenSolaris install you might wish to try the following:
<pre>
opensolaris$ sudo ./ref/debug/examples/pgmsend -n "pcn0;239.192.0.1" ichigo milk<br>
</pre>

## PGM Testing on Multicast Loopback ##
For multicast loopback testing using only one host requires use of a low-resolution timing mechanism as the <tt>/dev/rtc</tt> real-time clock device cannot be shared.  The example <tt>pgmrecv</tt> and <tt>pgmsend</tt> applications have convenience parameters to automagically enable this configuration.  For this demonstration we will also use UDP encapsulation of the PGM protocol so as not to require any extra system privileges.  On one terminal start a receiver.
<pre>
aiko$ ./ref/debug/examples/pgmrecv -lp 3065<br>
** Message: pgmrecv<br>
2008-11-19 17:59:05 aiko: setting low resolution timers for multicast loopback.<br>
2008-11-19 17:59:05 aiko: scheduling startup.<br>
2008-11-19 17:59:05 aiko: entering main event loop ...<br>
2008-11-19 17:59:05 aiko: startup.<br>
2008-11-19 17:59:05 aiko: create transport.<br>
2008-11-19 17:59:05 aiko: startup complete.<br>
</pre>
In another terminal send some test messages:
<pre>
$ ./ref/debug/examples/pgmsend -lp 3065 mooo baa<br>
2008-11-19 17:59:18 aiko: setting low resolution timers for multicast loopback.<br>
</pre>
In the receiving terminal you should see the correct receipt of two messages.
<pre>
2008-11-19 17:59:18 aiko: (5 bytes)<br>
2008-11-19 17:59:18 aiko: 	1: "mooo" (5 bytes)<br>
2008-11-19 17:59:18 aiko: (4 bytes)<br>
2008-11-19 17:59:18 aiko: 	1: "baa" (4 bytes)<br>
</pre>
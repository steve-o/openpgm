#summary OpenPGM 2 : C Reference : Build Library : RHEL 4
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building for RHEL 4 or CentOS 4 ###
Don't, for the following reasons.

#### Issues ####
  1. <tt>pkg-config</tt> does not support <tt>--static</tt> to provide static library parameters for GLib and other packages.
  1. <tt>libsoup-devel-2.2.1-4.i386.rpm</tt> is broken.
  1. Old <tt>gcc</tt> does not support new warnings, <tt>-Wunsafe-loop-optimizations</tt>.
  1. GLib 2.10 is required for the [SLAB allocator](http://library.gnome.org/devel/glib/stable/glib-Memory-Slices.html), the default packaged version is 2.4.  A special 2.12 build is available as a dependency for Evolution, <tt>evolution28-glib2-devel.i386</tt>.
  1. <tt>libsoup</tt> 2.2.1 does not support asynchronous callbacks, however the <tt>evolution28-libsoup-devel.i386</tt> does.
  1. <tt>evolution28-libsoup-devel.i386</tt> has an extra hidden dependency on <tt>gobject</tt>.


#### Rebuilding yum packages ####
Setup user packaging as per [YumAndRPM](http://wiki.centos.org/TipsAndTricks/YumAndRPM) in the CentOS wiki.  Download the source package from Red Hat or CentOS, then rebuild the package, installing any additional build dependencies as necessary.
<pre>
$ wget http://mirror.centos.org/centos/4/os/SRPMS/libsoup-2.2.1-4.src.rpm<br>
$ sudo yum install gnutls-devel<br>
$ rpmbuild --rebuild libsoup-*.src.rpm<br>
$ sudo rpm -i redat/RPMS/libsoup-devel-*.rpm<br>
</pre>

#### Building ####
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
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
For the default debug build use the following and look in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.RHEL4<br>
</pre>

#### Testing ####
The lack of static library support in <tt>pkg-config</tt> requires extra steps for testing, Red Hat <tt>sudo</tt> by default blocks <tt>LD_LIBRARY_PATH</tt> exports.
<pre>
$ sudo bash<br>
# LD_LIBRARY_PATH=/usr/evolution28/lib ./pgmrecv<br>
</pre>
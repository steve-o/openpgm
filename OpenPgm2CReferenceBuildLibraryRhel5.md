#summary OpenPGM 2 : C Reference : Build Library : RHEL 5
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
## Building for RHEL 5 or CentOS 5 ##
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
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ scons -f SConstruct.097<br>
</pre>
By default SCons is configured to build the debug tree in <tt>./ref/debug</tt> to build the release version in <tt>./ref/release</tt>.
<pre>
$ scons -f SConstruct.097 BUILD=release<br>
</pre>
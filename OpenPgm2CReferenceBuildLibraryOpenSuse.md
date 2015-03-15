#summary OpenPGM 2 : C Reference : Build Library : openSUSE 11.1
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building for openSUSE 11.1 ###
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
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build without the ncurses examples as <tt>libpanel</tt> is not included.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ scons WITH_NCURSES=false<br>
</pre>
By default SCons is configured to build the debug tree in <tt>./ref/debug</tt> to build the release version in <tt>./ref/release</tt>.
<pre>
$ scons BUILD=release WITH_NCURSES=false<br>
</pre>
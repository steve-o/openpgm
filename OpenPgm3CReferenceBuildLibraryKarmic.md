#summary OpenPGM 3 : C Reference : Build Library : GCC 4 on Ubuntu 9.04 through 10.04
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC for Ubuntu 9.04 through 10.04 ###
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ *sudo apt-get install build-essential scons subversion*<br>
</pre>
Install the library optional dependencies, primarily GLib and net-snmp libraries.  libncurses is only required for some examples.
<pre>
$ *sudo apt-get install libglib2.0-dev libncurses5-dev libsnmp-dev*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout http://openpgm.googlecode.com/svn/tags/release-3-0-43 *<br>
</pre>
Build.
<pre>
$ *cd release-3-0-43/openpgm/pgm*<br>
$ *scons WITH_GLIB=true WITH_GETTEXT=true*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-Linux-x86_64</tt> or <tt>./ref/debug-Linux-i686</tt> dependent upon your machine.

To build the release version in <tt>./ref/release-Linux-x86_64</tt> use the following:
<pre>
$ *scons BUILD=release WITH_GLIB=true WITH_GETTEXT=true*<br>
</pre>
#summary OpenPGM 2 : C Reference : Build Library : Sun Studio 12u1 on Ubuntu
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building with Sun Studio 12u1 for Ubuntu 8.04 or 8.10 ###
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ sudo apt-get install build-essential scons subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.2-dev libncurses5-dev libsnmp-dev<br>
</pre>
Install the Sun Studio following provided instructions.  Prepare a helper source shell script or merge with the default environment.
<pre>
$ cat - | sudo dd of=/opt/sun/sunstudiovars.sh<br>
#!/bin/sh<br>
if [ -z "${PATH}" ]<br>
then<br>
PATH="/opt/sun/sunstudio12.1/bin"; export PATH<br>
else<br>
PATH="/opt/sun/sunstudio12.1/bin:${PATH}"; export PATH<br>
fi<br>
if [ -z "${MANPATH}" ]<br>
then<br>
MANPATH="/opt/sun/sunstudio12.1/man"; export MANPATH<br>
else<br>
MANPATH="/opt/sun/sunstudio12.1/man:${MANPATH}"; export MANPATH<br>
fi<br>
^D<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ . /opt/sun/sunstudiovars.sh<br>
$ scons -f SConstruct.097.sunstudio<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.097.sunstudio<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.097.sunstudio BUILD=release<br>
</pre>
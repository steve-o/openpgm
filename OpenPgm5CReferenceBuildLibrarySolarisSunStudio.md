#summary OpenPGM 5 : C Reference : Build Library : Sun Studio 12u1 on Solaris
#labels Phase-Implementation
#sidebar TOC5CReferenceProgrammersChecklist
### Building with Sun Studio 12u1 for Solaris 10 SPARC ###
First install all the C compiler dependencies (including OS patches as needed), <tt>sudo</tt> and SCons.  Visit the Oracle website for the latest recommended patches to apply.  Sudo is included on the Solaris 10 Companion Software CD and needs to be set setuid root and configured with <tt>visudo</tt>.  Scons must be installed with the default Python and not <tt>/usr/sfw/bin/python</tt>.

Install the Sun Studio following provided instructions.  Prepare a helper source shell script or merge with the default environment.
<pre>
$ *cat - | sudo dd of=/opt/sunstudio12.1/sunstudiovars.sh<br>
#!/bin/sh<br>
if [ -z "${PATH}" ]<br>
then<br>
PATH="/opt/sunstudio12.1/bin"; export PATH<br>
else<br>
PATH="/opt/sunstudio12.1/bin:${PATH}"; export PATH<br>
fi<br>
if [ -z "${MANPATH}" ]<br>
then<br>
MANPATH="/opt/sunstudio12.1/man:/usr/share/man"; export MANPATH<br>
else<br>
MANPATH="/opt/sunstudio12.1/man:${MANPATH}"; export MANPATH<br>
fi<br>
^D*<br>
</pre>
For convenience some executables are symlinked into the path.
<pre>
$ *su -*<br>
# *cd /usr/bin*<br>
# *chmod +s /usr/sfw/bin/sudo*<br>
# *ln -s /usr/sfw/bin/sudo*<br>
</pre>
Scons must be downloaded from http://sourceforge.net/projects/scons/files/
<pre>
$ */usr/sfw/bin/wget http://sourceforge.net/projects/scons/files/scons/1.2.0/scons-1.2.0.tar.gz/download *<br>
$ *gzcat scons-1.2.0.tar.gz | tar xvf -*<br>
$ *cd scons-1.2.0*<br>
$ *sudo python setup.py install*<br>
</pre>
Optionally, install the library dependencies, primarily GLib and pkg-config, this tutorial install everything into <tt>/opt/glib-sunstudio</tt>.  <tt>pkg-config</tt> is a binary and doesn't need to be specially built for v9 architecture.
<pre>
$ */usr/sfw/bin/wget http://pkgconfig.freedesktop.org/releases/pkg-config-0.23.tar.gz *<br>
$ *gzcat pkg-config-0.23.tar.gz | tar xvf -*<br>
$ *cd pkg-config-0.23*<br>
$ *PATH=/usr/ccs/bin:$PATH ./configure --with-installed-glib --prefix=/opt/glib-sunstudio*<br>
$ *PATH=/usr/ccs/bin:$PATH make*<br>
$ *sudo /usr/ccs/bin/make install*<br>
$ *CFLAGS='-m64' LIBS='-lresolv -lsocket -lnsl' PATH=/opt/glib-sunstudio/bin:/usr/ccs/bin:$PATH \*<br>
*./configure --prefix=/opt/glib-sunstudio*<br>
$ *PATH=/usr/ccs/bin:$PATH make*<br>
$ *sudo PATH=/usr/ccs/bin:$PATH /usr/ccs/bin/make install*<br>
</pre>
If you have subversion installed (requires a significant amount of dependencies), checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout), otherwise download the latest tarball.
<pre>
$ *svn checkout http://openpgm.googlecode.com/svn/tags/release-5-0-91 libpgm-5.0.91 *<br>
$ *wget http://openpgm.googlecode.com/files/libpgm-5.0.91.tar.gz *<br>
</pre>
Build.
<pre>
$ *cd libpgm-5.0.91/openpgm/pgm*<br>
$ *. /opt/sunstudio12.1/sunstudiovars.sh*<br>
$ *scons -f SConstruct.Solaris.sunstudio WITH_GLIB=true WITH_GETTEXT=true*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-SunOS-sun4u-sunstudio</tt>.

To build the release version in <tt>./ref/release-SunOS-sun4u-sunstudio</tt> use the following:
<pre>
$ *scons -f SConstruct.Solaris.sunstudio BUILD=release WITH_GLIB=true WITH_GETTEXT=true*<br>
</pre>


#### SNMP ####
On the Solaris 10 DVD you can find pre-compiled packages for Net-SNMP, ensure the following are installed, alternatively you can install Net-SNMP from an alternative repository or build from source.
<pre>
SUNWsmmgw - Init scripts<br>
SUNWsmcmd - Executables<br>
SUNWsmagt - Libraries<br>
SUNWsmaS  - Headers and source<br>
</pre>

Note that <tt>net-snmp-config-64</tt> must be called twice, once for CFLAGS and once for LIBS.

To build the release version with SNMP support use the following:
<pre>
$ *scons -f SConstruct.Solaris.sunstudio BUILD=release WITH_SNMP=true WITH_GLIB=true*<br>
</pre>


#### Testing ####
The dynamic GLib libraries require an extra step for testing, for example,
<pre>
$ *sudo LD_LIBRARY_PATH=/opt/glib-sunstudio/lib:$LD_LIBRARY_PATH ./ref/release/pgmrecv*<br>
</pre>


#### Unit testing ####
Unit testing depends upon the Check unit test suite and must be downloaded and compiled for each compiler.  Unfortunately the current Check source <tt>configure</tt> script does not support non-GNU environments too well and <tt>gmake</tt> must be used instead of Sun <tt>make</tt>.
<pre>
$ *wget http://downloads.sourceforge.net/project/check/check/0.9.8/check-0.9.8.tar.gz/download *<br>
$ *gzcat check-0.9.8.tar.gz  | tar xvf -*<br>
$ *cd check-0.9.8*<br>
$ *CFLAGS='-m64' PATH=/usr/ccs/bin:$PATH \*<br>
*./configure --prefix=/opt/glib-sunstudio --disable-shared*<br>
$ *PATH=/usr/ccs/bin:/usr/sfw/bin:$PATH gmake*<br>
$ *sudo PATH=/usr/ccs/bin:/usr/sfw/bin:$PATH /usr/sfw/bin/gmake install*<br>
</pre>


#### Performance testing ####
Support for Google Protobuf 2.3.0 is broken for Sun Studio 12u1, unfortunately Google do not use Solaris/SPARC and so monitor the ticket for further progress.

http://code.google.com/p/protobuf/issues/detail?id=178

As a workaround use the Sun GCC 32-bit compiler.
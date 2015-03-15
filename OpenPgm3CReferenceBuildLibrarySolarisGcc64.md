#summary OpenPGM 3 : C Reference : Build Library : GCC 3.4.6 on Solaris (64-bit)
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Building with GCC 3.4.6 for Solaris 10 SPARC ###
First install all the C compiler dependencies, <tt>sudo</tt> and SCons.  GCC can be downloaded from http://sunfreeware.com and <tt>sudo</tt> is included on the Solaris 10 Companion Software CD, <tt>sudo</tt> must be set setuid root and configured with <tt>visudo</tt>.  Scons must be installed with the default Python and not <tt>/usr/sfw/bin/python</tt>.

For convenience some executables are symlinked into the path.
<pre>
$ *su -*<br>
# *cd /usr/bin*<br>
# *chmod +s /usr/sfw/bin/sudo*<br>
# *ln -s /usr/sfw/bin/sudo*<br>
</pre>
Install the 64-bit capable GCC compiler, by default installs to <tt>/usr/local</tt>.
<pre>
$ */usr/sfw/bin/wget \*<br>
*ftp://ftp.sunfreeware.com/pub/freeware/sparc/10/gcc-3.4.6-sol10-sparc-local.gz *<br>
$ *gzip -d gcc-3.4.6-sol10-sparc-local.gz*<br>
$ *sudo /usr/sbin/pkgadd -d gcc-3.4.6-sol10-sparc-local*<br>
</pre>
Optionally, install libiconv as a dependency for GLib, it must be downloaded and built from source as sunfreeware.com do not include 64-bit binaries.
<pre>
$ */usr/sfw/bin/wget \*<br>
*ftp://ftp.sunfreeware.com/pub/freeware/sparc/10/libiconv-1.13.1-sol10-sparc-local.gz *<br>
$ *gzip -d libiconv-1.13.1-sol10-sparc-local.gz*<br>
$ *sudo /usr/sbin/pkgadd -d libiconv-1.13.1-sol10-sparc-local*<br>
$ *get ftp://ftp.sunfreeware.com/pub/freeware/SOURCES/libiconv-1.13.1.tar.gz *<br>
$ *gzip -d libiconv-1.13.1.tar.gz*<br>
$ *tar xf libiconv-1.13.1.tar*<br>
$ *MAKE=gmake AR=gar RANLIB=granlib STRIP=gstrip AS=gas CC=gcc CFLAGS='-m64' \*<br>
*PATH=/usr/local/bin:/usr/sfw/bin:$PATH \*<br>
*./configure --prefix=/usr/local --libdir=/usr/local/lib/sparcv9*<br>
$ *PATH=/usr/local/bin:/usr/sfw/bin:$PATH gmake*<br>
$ *sudo PATH=/usr/local/bin:/usr/sfw/bin:$PATH /usr/sfw/bin/gmake install*<br>
</pre>
Scons must be downloaded from http://sourceforge.net/projects/scons/files/
<pre>
$ */usr/sfw/bin/wget \*<br>
*http://sourceforge.net/projects/scons/files/scons/1.2.0/scons-1.2.0.tar.gz/download *<br>
$ *gzcat scons-1.2.0.tar.gz | tar xvf -*<br>
$ *cd scons-1.2.0*<br>
$ *sudo python setup.py install*<br>
</pre>
Optionally, install the remaining library dependencies, primarily GLib and pkg-config, this tutorial install everything into <tt>/opt/glib-gcc64</tt>.  <tt>pkg-config</tt> is a binary and doesn't need to be specially built for v9 architecture.
<pre>
$ */usr/sfw/bin/wget http://pkgconfig.freedesktop.org/releases/pkg-config-0.23.tar.gz *<br>
$ *gzcat pkg-config-0.23.tar.gz | tar xvf -*<br>
$ *cd pkg-config-0.23*<br>
$ *MAKE=gmake AR=gar RANLIB=granlib STRIP=gstrip AS=gas CC=gcc \*<br>
*PATH=/usr/local/bin:/usr/sfw/bin:$PATH \*<br>
*./configure --with-installed-glib --prefix=/opt/glib-gcc64*<br>
$ *PATH=/usr/local/bin:/usr/sfw/bin:$PATH gmake*<br>
$ *sudo PATH=/usr/local/bin:/usr/sfw/bin:$PATH /usr/sfw/bin/gmake install*<br>
$ *MAKE=gmake AR=gar RANLIB=granlib STRIP=gstrip AS=gas CC=gcc CFLAGS='-m64' \*<br>
*LIBS='-lresolv -lsocket -lnsl' \*<br>
*PATH=/opt/glib-gcc64/bin:/usr/local/bin:/usr/sfw/bin:$PATH \*<br>
*./configure --prefix=/opt/glib-gcc64*<br>
$ *PATH=/usr/local/bin:/usr/sfw/bin:$PATH gmake*<br>
$ *sudo PATH=/usr/local/bin:/usr/sfw/bin:$PATH /usr/sfw/bin/gmake install*<br>
</pre>
If you have subversion installed (requires a significant amount of dependencies), checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout), otherwise download the latest tarball.
<pre>
$ */usr/sfw/bin/wget http://openpgm.googlecode.com/files/libpgm-3.0.43.tar.gz *<br>
$ *gzcat libpgm-3.0.43.tar.gz | xvf -*<br>
</pre>
Build.
<pre>
$ *cd libpgm-3.0.43/openpgm/pgm*<br>
$ *scons -f SConstruct.Solaris.gcc64 WITH_GLIB=true*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-SunOS-sun4u-gcc64</tt>.

To build the release version in <tt>./ref/release-SunOS-sun4u-gcc64</tt> use the following:
<pre>
$ *scons -f SConstruct.Solaris.gcc64 BUILD=release WITH_GLIB=true*<br>
</pre>

#### Testing ####
The dynamic GLib libraries require an extra step for testing, for example,
<pre>
$ *sudo LD_LIBRARY_PATH=/opt/glib-gcc64/lib:$LD_LIBRARY_PATH \*<br>
*./ref/release-SunOS-sun4u-gcc64/pgmrecv*<br>
</pre>


#### Unit testing ####
Unit testing depends upon the Check unit test suite and must be downloaded and compiled for each compiler.
<pre>
$ */usr/sfw/bin/wget \*<br>
*http://downloads.sourceforge.net/project/check/check/0.9.8/check-0.9.8.tar.gz/download *<br>
$ *gzcat check-0.9.8.tar.gz  | tar xvf -*<br>
$ *cd check-0.9.8*<br>
$ *MAKE=gmake AR=gar RANLIB=granlib STRIP=gstrip AS=gas CC=gcc \*<br>
*CFLAGS='-m64 -I/usr/local/include' LDFLAGS='-L/usr/local/lib' \*<br>
*LIBS='-lresolv -lsocket -lnsl' \*<br>
*PATH=/opt/glib-gcc64/bin:/usr/local/bin:/usr/sfw/bin:$PATH \*<br>
*./configure --prefix=/opt/glib-gcc64 --disable-shared*<br>
$ *PATH=/usr/local/bin:/usr/sfw/bin:$PATH gmake*<br>
$ *sudo PATH=/usr/local/bin:/usr/sfw/bin:$PATH /usr/sfw/bin/gmake install*<br>
</pre>


#### Performance testing ####
Support for Google Protobuf 2.3.0 is broken for GCC 64-bit, unfortunately Google do not use Solaris/SPARC and so monitor the ticket for further progress.

http://code.google.com/p/protobuf/issues/detail?id=101

As a workaround use the Sun GCC 32-bit compiler.
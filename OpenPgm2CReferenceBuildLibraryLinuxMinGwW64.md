#summary OpenPGM 2 : C Reference : Build Library : Microsoft Windows (64-bit)
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building for 64-bit Microsoft Windows ###
Microsoft Windows is supported by cross-compiling on Ubuntu 8.04 with MinGW-w64.  MSVC9 (C89) is not supported for building OpenPGM however you may build your application and link to a MinGW built OpenPGM library.  MinGW-w64 is not within the Canonical repositories for Ubuntu, nor part of the official MinGW project.

First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ sudo apt-get install build-essential scons subversion<br>
</pre>
Download and unpack [MinGW-w64](http://sourceforge.net/projects/mingw-w64/files/).
<pre>
$ wget http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Release%20for%20GCC%204.4.1/mingw-w64-bin_x86-64-linux_4.4.1-1a.tar.bz2/download<br>
$ sudo mkdir /opt/mingw<br>
$ sudo tar xjf -C /opt/mingw mingw-w64-*<br>
</pre>
Setup an environment for MinGW.
<pre>
$ cat - | sudo tee mingwvars.sh<br>
#!/bin/sh<br>
if [ -z "${PATH}" ]<br>
then<br>
PATH="/opt/mingw/bin"; export PATH<br>
else<br>
PATH="/opt/mingw/bin:${PATH}"; export PATH<br>
fi<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
$ cd release-2-1/openpgm/pgm<br>
</pre>
Download and unpack the Win64 library dependencies, primarily GLib Binaries & Dev, and proxy-libintl Binaries.  Extract into a subdirectory called <tt>win/</tt>.

  * http://ftp.gnome.org/pub/GNOME/binaries/win64/
<pre>
$ cd win64<br>
$ wget http://ftp.gnome.org/pub/GNOME/binaries/win64/glib/2.20/glib_2.20.1-1_win64.zip  \<br>
http://ftp.gnome.org/pub/GNOME/binaries/win64/glib/2.20/glib-dev_2.20.1-1_win64.zip  \<br>
http://ftp.gnome.org/pub/GNOME/binaries/win64/dependencies/proxy-libintl-dev_20080918_win64.zip<br>
$ for i in *.zip; do unzip $i; done<br>
</pre>
Update the <tt>pkgconfig</tt> configuration for where the root directory is located.
<pre>
$ for i in lib/pkgconfig/*.pc; do sed -i "s#prefix=c:/.*#prefix=`pwd`#" $i; done<br>
</pre>
Patch the mingw-w64 installation with updated Windows headers.
<pre>
$ sudo patch -d /opt/mingw/x86_64-w64-mingw32/include < mingw-w64-bin_x86-64-linux_4.4.1-1openpgm1.diff<br>
</pre>
Build.
<pre>
$ cd ..<br>
$ . /opt/mingw/mingwvars.sh<br>
$ scons -f SConstruct.097.mingw64<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.097.mingw64<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.097.mingw64 BUILD=release<br>
</pre>
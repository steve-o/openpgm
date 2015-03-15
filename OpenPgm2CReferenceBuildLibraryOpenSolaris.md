#summary OpenPGM 2 : C Reference : Build Library : OpenSolaris 2008.11 or 2009.06
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building for OpenSolaris 2008.11 or 2009.06 ###
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
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
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
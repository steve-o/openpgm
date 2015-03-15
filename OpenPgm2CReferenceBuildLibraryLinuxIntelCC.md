#summary OpenPGM 2 : C Reference : Build Library : Intel C++ Compiler
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
### Building with Intel C++ Compiler 11.1 for Ubuntu 8.04 or 8.10 ###
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ sudo apt-get install build-essential scons subversion<br>
</pre>
Install the library dependencies, primarily GLib, libsoup, and net-snmp libraries.  libncurses is only required for some examples.  Note that the repository contains multiple versions of libsoup with incompatible API.
<pre>
$ sudo apt-get install libglib2.0-dev libsoup2.2-dev libncurses5-dev libsnmp-dev<br>
</pre>
Install the Intel Compiler Suite following provided instructions.
<pre>
$ tar zxf l_cproc_p_11.1.064_intel64.tgz<br>
$ cd l_cproc_p_11.1.064_intel64<br>
$ sudo ./install.sh<br>
</pre>
Patch Scons to support version 11 of the compiler.
<pre>
$ cd /usr/lib/scons/SCons/Tool<br>
$ sudo patch <<br>
--- /usr/lib/scons/SCons/Tool/intelc.py	2007-12-04 04:20:54.000000000 +0800<br>
+++ /tmp/intelc.py	2010-01-21 13:50:46.000000000 +0800<br>
@@ -210,6 +210,10 @@<br>
# Typical dir here is /opt/intel/cc/9.0 for IA32,<br>
# /opt/intel/cce/9.0 for EMT64 (AMD64)<br>
versions.append(re.search(r'([0-9.]+)$', d).group(1))<br>
+        for d in glob.glob('/opt/intel/Compiler/*'):<br>
+            # Typical dir here is /opt/intel/cc/9.0 for IA32,<br>
+            # /opt/intel/cce/9.0 for EMT64 (AMD64)<br>
+            versions.append(re.search(r'([0-9.]+)$', d).group(1))<br>
versions = uniquify(versions)       # remove dups<br>
versions.sort(vercmp)<br>
return versions<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ svn checkout http://openpgm.googlecode.com/svn/branches/release-2-1<br>
</pre>
Build.
<pre>
$ cd release-2-1/openpgm/pgm<br>
$ . /opt/intel/Compiler/11.1/064/bin/iccvars.sh<br>
$ scons -f SConstruct.097.intelc<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug</tt>:
<pre>
$ scons -f SConstruct.097.intelc<br>
</pre>
To build the release version in <tt>./ref/release</tt> use the following:
<pre>
$ scons -f SConstruct.097.intelc BUILD=release<br>
</pre>
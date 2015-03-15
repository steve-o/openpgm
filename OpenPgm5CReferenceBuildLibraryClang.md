#summary OpenPGM 5 : C Reference : Build Library : Clang on Ubuntu 10.04
#labels Phase-Implementation
#sidebar TOC5CReferenceProgrammersChecklist
### Building with Clang for Ubuntu 10.04 ###
First install all the C compiler dependencies, SCons, and Subversion.
<pre>
$ *sudo apt-get install build-essential scons subversion clang*<br>
</pre>
Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout).
<pre>
$ *svn checkout http://openpgm.googlecode.com/svn/tags/release-5-0-91 libpgm-5.0.91 *<br>
</pre>
Build.
<pre>
$ *cd libpgm-5.0.91/openpgm/pgm*<br>
$ *scons -f SConstruct.clang*<br>
</pre>
By default SCons is configured to build a debug tree in <tt>./ref/debug-Linux-x86_64-clang</tt> or <tt>./ref/debug-Linux-i686-clang</tt> dependent upon your machine.

To build the release version in <tt>./ref/release-Linux-x86-64-clang</tt> use the following:
<pre>
$ *scons -f SConstruct.clang BUILD=release*<br>
</pre>
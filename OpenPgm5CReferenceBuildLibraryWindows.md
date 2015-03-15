#summary OpenPGM 5 : C Reference : Build Library : MSVC 2010 on Windows 7
#labels Phase-Implementation
#sidebar TOC5CReferenceProgrammersChecklist
### Building with MSVC 2010 for Windows 7 ###
First install [Microsoft Visual C++ 2010 Express](http://www.microsoft.com/express/Windows/), [Professional](http://www.microsoft.com/visualstudio/en-us/) (x64 support),
etc.  Install the build system [CMake](http://www.cmake.org/cmake/resources/software.html) and the PGM build dependencies [Perl](http://strawberryperl.com/) and [Python 2.7](http://www.python.org/download/).

Checkout the repository as per the [Subversion guide](http://code.google.com/p/openpgm/source/checkout), or download the latest source archive and extract.  You can use Cygwin and Subversion to checkout
the source.
<pre>
$ *cd /cygdrive/c *<br>
$ *svn checkout http://openpgm.googlecode.com/svn/tags/release-5-1-102 libpgm-5.1.102 *<br>
</pre>
Configure using a Visual Studio Command Prompt.
<pre>
C:\> *cd \libpgm-5.1.102\openpgm\pgm*<br>
C:\libpgm-5.1.102\openpgm\pgm> *mkdir build*<br>
C:\libpgm-5.1.102\openpgm\pgm> *cd build*<br>
C:\libpgm-5.1.102\openpgm\pgm\build> *cmake ..*<br>
</pre>
CMake will build a Makefile in the current directory with the PGM library to be built in the sub-directory <tt>lib</tt>.

To configure the release version use the following command or change the parameter using <tt>cmake-gui</tt>.
<pre>
C:\libpgm-5.1.102\openpgm\pgm\build> *cmake -DCMAKE_BUILD_TYPE=Release ..*<br>
</pre>

Build.
<pre>
C:\libpgm-5.1.102\openpgm\pgm\build> *nmake*<br>
</pre>

If you want a redistributable installer use CPack to build the package.
<pre>
C:\libpgm-5.1.102\openpgm\pgm\build> *nmake package*<br>
</pre>
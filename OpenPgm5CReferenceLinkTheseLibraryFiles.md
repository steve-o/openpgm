#summary OpenPGM 5 C Reference : Link these Library Files
#labels Phase-Implementation
#sidebar TOC5CReferenceProgrammersChecklist
OpenPGM C programs must link the appropriate library files.


<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Linker Flag</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Communications and Events</h3></td>
</tr><tr>
<td><tt>-lpgm</tt></td>
<td>All OpenPGM programs must use this link flag.</td>
</tr><tr>
<td><tt>-lm</tt></td>
<td>Required for support of histogram math enabled via <tt>-DUSE_HISTOGRAMS</tt>.</td>
</tr><tr>
<td><tt>-lrt</tt></td>
<td>Required for <tt>clock_gettime()</tt> support.  No longer required with GLIBC 2.17+.</td>
</tr><tr>
<td><tt>-lresolv -lsocket -lnsl</tt></td>
<td>Required for Solaris socket support.</td>
</tr><tr>
<td><h3>Monitoring and Administration</h3></td>
</tr><tr>
<td><tt>-lpgmhttp </tt></td>
<td>Include this for the HTTP/HTTPS interface.</td>
</tr><tr>
<td><tt>-lpgmsnmp <code>`</code>net-snmp-config --agent-libs<code>`</code></tt></td>
<td>Include this for a Net-SNMP based sub-agent.</td>
</tr>
</table>


## Microsoft Windows Cross Compiles ##

For Microsoft Windows 32-bit and 64-bit cross compile builds additional dependencies are
introduced from the compiler in addition to the Windows platform dependencies,

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Library</th>
<th>Description</th>
</tr>
<tr>
<td><tt>libgcc.a</tt></td>
<td>GCC cross-compiler C runtime library.</td>
</tr><tr>
<td><tt>libmingwex.a</tt></td>
<td>Glibc compatibility runtime library.</td>
</tr><tr>
<td><tt>iphlpapi.lib</tt></td>
<td>Interface enumeration support.</td>
</tr><tr>
<td><tt>ws2_32.lib</tt></td>
<td>Windows Sockets support.</td>
</tr><tr>
<td><tt>winmm.lib</tt></td>
<td>Windows high resolution timers.</td>
</tr>
</table>

To build the example applications in MSVC use the following screenshots for configuration.

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-unicode.png'><img src='http://miru.hk/wiki/320px-msvc-unicode.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Tag source files as Unicode</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-compile as.png'><img src='http://miru.hk/wiki/320px-msvc-compile as.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Compile as C++ Code</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-includes.png'><img src='http://miru.hk/wiki/320px-msvc-includes.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Additional Include Directories</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-crt.png'><img src='http://miru.hk/wiki/320px-msvc-crt.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Multi-Threaded Runtime Library</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-libpath.png'><img src='http://miru.hk/wiki/320px-msvc-libpath.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Additional Library Directories</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc-libs.png'><img src='http://miru.hk/wiki/320px-msvc-libs.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Additional Dependencies</td>
</tr>
</table>

## Microsoft Visual Studio Native ##

Library dependencies are automagically pulled in through the compilers support for <tt><a href='http://msdn.microsoft.com/en-us/library/7f0aews7(v=vs.71).aspx'>#pragma comment</a></tt>.  It is only necessary to specify directories and any compiler preferences.

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc2010-compile as.png'><img src='http://miru.hk/wiki/320px-msvc2010-compile as.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Compile as C++ Code</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc2010-includes.png'><img src='http://miru.hk/wiki/320px-msvc2010-includes.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Additional Include Directories</td>
</tr>
</table>

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<td><a href='http://miru.hk/wiki/msvc2010-libpath.png'><img src='http://miru.hk/wiki/320px-msvc2010-libpath.png' /><br /><br /><img src='http://miru.hk/wiki/magnify-clip.png' align='right' /></a>Additional Library Directories</td>
</tr>
</table>
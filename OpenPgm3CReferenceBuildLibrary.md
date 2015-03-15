#summary OpenPGM 3 : C Reference : Build Library
#labels Phase-Implementation
#sidebar TOC3CReferenceProgrammersChecklist
### Introduction ###
There are currently no official releases or pre-built packages in any distributions.  The OpenPGM library must be built from source taken from the projects subversion repository or from a downloaded tarball.


### Supported Platforms ###
Platforms supported by paid contract.
  * [GCC 4.2.4-4.4.3](OpenPgm3CReferenceBuildLibraryKarmic.md) on Ubuntu 9.04 through 10.04
  * [Clang 2.7](OpenPgm3CReferenceBuildLibraryClang.md) on Ubuntu 10.04
  * [GCC 3.4.3](OpenPgm3CReferenceBuildLibrarySolarisSunGcc.md) on Solaris 10 on SPARC (32-bit), supplied on Solaris 10 Companion Software CD
  * [GCC 3.4.6](OpenPgm3CReferenceBuildLibrarySolarisGcc64.md) on Solaris 10 on SPARC (64-bit)
  * [Sun Studio 12u1](OpenPgm3CReferenceBuildLibrarySolarisSunStudio.md) on Solaris 10 on SPARC (64-bit)

### Untested Platforms ###
<img src='http://miru.hk/wiki/womm.png' align='right' width='200' height='193' />Functional build but no available test report.
  * [GCC 4.2.1](OpenPgm3CReferenceBuildLibraryFreeBSD.md) on FreeBSD 8.0
  * [GCC 4.2.1](OpenPgm3CReferenceBuildLibraryLinuxMinGW.md) on Microsoft Windows XP, cross-compiled on Ubuntu 9.04 through 10.04 using MinGW.
  * [GCC 4.2.1](OpenPgm3CReferenceBuildLibraryLinuxWine.md) on Wine 1.1.31, cross-compiled on Ubuntu 9.04 through 10.04 using MinGW.
  * [GCC 3.4.3](OpenPgm3CReferenceBuildLibraryOpenSolaris.md) on OpenSolaris 2009.06
  * [GCC 4.2.1](OpenPgm3CReferenceBuildLibraryOSX.md) on OS X 10.6
  * GCC on SUSE Linux Enterprise Server 10.2
  * GCC on OpenSUSE 11.2
  * GCC on Red Hat Enterprise Linux, or CentOS 5.4
  * MinGW-w64 on Ubuntu 8.04
  * Intel C Compiler on Ubuntu 8.04
  * Sun Studio on Linux on Ubuntu 8.04

### Unsupported Platforms ###
Platforms supported in OpenPGM version 2 but not supported in OpenPGM version 3.
  * GCC on Debian 4
  * GCC on Red Hat Enterprise Linux, or CentOS 4.8

### Unsupported Compilers ###
Compilers known not to work or have not been visited.
  * MSVC on Microsoft Windows
  * LCC on Microsoft Windows
  * GCC 3.4.3 on Solaris 10 on SPARC (64-bit)
  * Open64 on Ubuntu 8.04
  * Open Watcom on Ubuntu 8.04
  * aCC on HP/UX 11i
  * xlC on Ubuntu 8.04 or AIX 6.1


### PGM Testing ###
Two hosts are required for full PGM protocol testing, one to send the other to receive.  In this example <tt>ayaka</tt> is the sending host and <tt>kiku</tt> is receiving.

On the receiving host run the OpenPGM receiver.
<pre>
kiku$ *sudo ./ref/debug/examples/pgmrecv*<br>
** Message: pgmrecv<br>
2008-05-27 18:02:53 kiku: scheduling startup.<br>
2008-05-27 18:02:53 kiku: entering main event loop ...<br>
2008-05-27 18:02:53 kiku: startup.<br>
2008-05-27 18:02:53 kiku: create transport.<br>
2008-05-27 18:02:53 kiku: startup complete.<br>
</pre>
On the sending host run the OpenPGM publisher.
<pre>
ayaka$ *sudo ./ref/debug/examples/pgmsend mooooo baa*<br>
</pre>
Then on the receiver you should see the test messages.
<pre>
2008-05-27 18:03:25 kiku: (7 bytes)<br>
2008-05-27 18:03:25 kiku: 	1: "mooooo" (7 bytes)<br>
2008-05-27 18:03:25 kiku: (4 bytes)<br>
2008-05-27 18:03:25 kiku: 	1: "baa" (4 bytes)<br>
</pre>
Without explicit network parameter passing OpenPGM will assume the default adapter and either IPv4 or IPv6 addressing.  If your operating system defaults to DHCP it is possible that the nodename of the host resolves to localhost and not a real adapter, it is then necessary to explicitly set the adapter name.  Similarly you might have problems with IPv6 auto-configuration and multiple scopes per adapter.  So on a default OpenSolaris install you might wish to try the following:
<pre>
opensolaris$ *sudo ./ref/debug/examples/pgmsend -n "pcn0;239.192.0.1" ichigo milk*<br>
</pre>

### PGM Testing on Multicast Loopback ###

For multicast loopback testing using only one host requires use of a low-resolution timing mechanism as the <tt>/dev/rtc</tt> real-time clock device cannot be shared.  The example <tt>pgmrecv</tt> and <tt>pgmsend</tt> applications have convenience parameters to automagically enable this configuration.  For this demonstration we will also use UDP encapsulation of the PGM protocol so as not to require any extra system privileges.  On one terminal start a receiver.
<pre>
aiko$ *./ref/debug/examples/pgmrecv -lp 3065*<br>
** Message: pgmrecv<br>
2008-11-19 17:59:05 aiko: scheduling startup.<br>
2008-11-19 17:59:05 aiko: entering main event loop ...<br>
2008-11-19 17:59:05 aiko: startup.<br>
2008-11-19 17:59:05 aiko: create transport.<br>
2008-11-19 17:59:05 aiko: startup complete.<br>
</pre>
In another terminal send some test messages:
<pre>
$ *./ref/debug/examples/pgmsend -lp 3065 mooo baa*<br>
</pre>
In the receiving terminal you should see the correct receipt of two messages.
<pre>
2008-11-19 17:59:18 aiko: (5 bytes)<br>
2008-11-19 17:59:18 aiko: 	1: "mooo" (5 bytes)<br>
2008-11-19 17:59:18 aiko: (4 bytes)<br>
2008-11-19 17:59:18 aiko: 	1: "baa" (4 bytes)<br>
</pre>
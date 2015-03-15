# History #

## 1998 January ##
  * _Pretty Good Multicast_, original draft <[draft-speakman-pgm-spec-00.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-00)>
  * Draft 1 <[draft-speakman-pgm-spec-01.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-01)>

<pre>
Deleted reference to proprietary trademark.<br>
</pre>

## 1998 August ##
  * Draft 2 <[draft-speakman-pgm-spec-02.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-02)>

<pre>
This revision benefited from general discussions in the forum of<br>
the Reliable Multicast IRTF as well as from individual discussion<br>
with Dan Leshchiner concerning source addressing and NAK elimina-<br>
tion, with Chetan Rai concerning outgoing packet ordering and<br>
local retransmission, and with Jim Gemmell, Luigi Rizzo, and<br>
Lorenzo Vicisano concerning FEC.<br>
<br>
Clarified that RDATA from DLRs and NCFs from network elements MUST<br>
bear the ODATA source's network-header source address.<br>
<br>
Added NAK elimination timer and corresponding procedures to net-<br>
work elements.<br>
<br>
Added procedures and packet formats to incorporate FEC.<br>
<br>
Changed all the packet type encodings to anticipate versioning and<br>
extension.<br>
<br>
Added work-in-progress items for RDATA delay at the source and<br>
minimum NAK back-off at receivers.<br>
<br>
Added work-in-progress items for SPMRs.<br>
</pre>

## 1999 June ##
  * Draft 3 <[draft-speakman-pgm-spec-03.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-03)>

<pre>
The polling and implosion control procedures in this document were<br>
developed jointly with Jim Gemmell.  The work on SPMRs arose from<br>
discussions with Dan Leshchiner.<br>
<br>
Removed range NAKs for re-working.<br>
<br>
Generalized and simplified methods for advancing transmit window.<br>
<br>
Removed increment sequence number from SPM packets.<br>
<br>
Removed Appendix B's information for congestion avoidance.<br>
<br>
Removed "local retransmission" in favor of full DLR functionality.<br>
<br>
Added generic polling capability within a single PGM hop.<br>
<br>
Added procedures to adjust NAK_BO_IVL dynamically and to address<br>
potential NAK implosion problems.<br>
<br>
Added SPMR procedures and packet formats.<br>
</pre>

## 1999 August ##
  * Cisco release IOS 12.0(5)T with Router-Assist <[PGM Router Assist](http://www.cisco.com/en/US/docs/ios/12_0t/12_0t5/feature/guide/pgmscale.html)>
  * Luigi Rizzo releases a PGM Host implementation for FreeBSD <[PGM Host](http://info.iet.unipi.it/~luigi/pgm.html)>

## 1999 October ##
  * Whitebarn release WRMF-PGM, a Reliable Multicast Protocol framework and Cisco PGM implementation.

## 2000 April ##
  * Draft 4 <[draft-speakman-pgm-spec-04.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-04)>

<pre>
Introduced NAK lists.<br>
<br>
Revised DLR procedures to include off-tree DLRs.<br>
<br>
Revised description of NAK procedures.<br>
<br>
Changed TPDU length in packet formats to TSDU length.<br>
<br>
Swap of SQN and TRAIL fields in ODATA/RDATA header formats.<br>
<br>
Removed RSN TSN Appendix (formerly Appendix C).<br>
<br>
Added FIN/SYN/RST support<br>
<br>
Defined SPM NLA = 0 to mean that no path information is present.<br>
<br>
Defined SPM_TRAIL/LEAD values when no windowing information is<br>
present.<br>
<br>
Rationalized the option number space. Note to implementors: this<br>
is a significant change, so make sure your options have the right<br>
numbers in the right order.<br>
<br>
Moved the bulk of the Transmit Window information to Appendix F.<br>
<br>
OPT_VAR_SIZE became OPT_VAR_PKTLEN, a more descriptive and useful<br>
name.<br>
</pre>

  * Cisco release IOS 12.1(1)T with PGM Host support (''defunct'') <[PGM Host](http://www.cisco.com/en/US/docs/ios/12_1t/12_1t1/feature/guide/dtpgmhst.html)>

## 2000 August ##
  * TIBCO announced it has delivered open source code <[PGM Open Source Program](http://www.prnewswire.com/cgi-bin/stories.pl?ACCT=104&STORY=/www/story/12-04-2000/0001378267&EDATE=)>

## 2000 November ##
  * Draft 5 <[draft-speakman-pgm-spec-05.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-05)>

<pre>
Added the specification allowing the combined use of fragmentation<br>
and FEC.<br>
<br>
Changed the text in the FEC appendix to disallow receivers to send<br>
selective NAKs when parity is available (in the "SHOULD NOT"<br>
form).<br>
<br>
Changed the text that deals with prioritization of packet<br>
transmission at the source.<br>
<br>
Lower-case "must", "should" .. etc changed to upper-case where<br>
needed.<br>
<br>
Added an "Intellectual Property" disclaimer.<br>
<br>
Fixed the packet format of OPT_FRAGMENT.<br>
<br>
Fixed some typos and minor inconsistencies.<br>
</pre>

  * Initial draft of full MIB for the PGM protocol <[draft-petrova-nwg-pgmmib-00](http://www.potaroo.net/ietf/old-ids/draft-petrova-nwg-pgmmib-00.txt)>

## 2001 February ##
  * Draft 6 <[draft-speakman-pgm-spec-06.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-06)>

<pre>
To accommodate very small transmit windows, relaxed the require-<br>
ment that SPMs be sent at least at the rate at which the transmit<br>
window is advanced.<br>
<br>
Deleted the recommendation that a source SHOULD transmit RDATA at<br>
priority over concurrent ODATA.<br>
<br>
Changed the use of multicast NAKs from a recommendation to an<br>
option.<br>
<br>
Revised the text throughout to prohibit selective NAKs in FEC ses-<br>
sion except for the leading partial transmission block.<br>
<br>
To accommodate shortening a transmission group after the transmis-<br>
sion of the last data packet, relaxed the requirement that<br>
OPT_CURR_TGSIZE be appended to the last data packet in a shortened<br>
group.<br>
<br>
Revised the procedures for handling fragments in an FEC session.<br>
<br>
Added rounds and their application to the polling procedures.<br>
<br>
Removed the option of setting SPM_PATH to zero.<br>
</pre>

  * Initial draft of PGMCC, a single rate multicast congestion control scheme <[draft-ietf-rmt-bb-pgmcc-00](http://www.tools.ietf.org/html/draft-ietf-rmt-bb-pgmcc-00)>

## 2001 July ##
  * Talarian contribute PGM inspection to Ethereal (Wireshark) <[packet-pgm](http://ethereal.netmirror.org/lists/ethereal-cvs/200107/msg00093.html)>

## 2001 September ##
  * Draft 7 <[draft-speakman-pgm-spec-07.txt](http://www.tools.ietf.org/html/draft-speakman-pgm-spec-07)>

## 2001 October ##
  * Microsoft releases Windows XP with PGM implemented in optional install of MSMQ 3.
  * Nortel Networks releases the 3.2 Passport 8600 Routing Switch with Router Assist <[Router Assist](http://www25.nortelnetworks.com/library/tpubs/html/passport/313196A/Chapter744.html#31843)>

## 2001 December ##
  * RFC 3208 (Experimental)

## 2002 May ##
  * TIBCO release Rendezvous 7.0 with PGM network protocol support.
  * Draft 2 of PGMMIB, full MIB for the PGM protocol <[draft-petrova-pgmmib-01.txt](http://www.icir.org/fenner/mibs/extracted/PGM-MIB-petrova-01.txt)>

## 2002 June ##
  * Draft 1 of PGMCC, a single rate multicast congestion control scheme <[draft-ietf-rmt-bb-pgmcc-01](http://www.tools.ietf.org/html/draft-ietf-rmt-bb-pgmcc-01)>

## 2003 April ##
  * Microsoft releases Windows Server 2003 with PGM implemented through Windows Sockets.

## 2003 June ##
  * Draft 2 of PGMCC, a single rate multicast congestion control scheme <[draft-ietf-rmt-bb-pgmcc-02](http://www.tools.ietf.org/html/draft-ietf-rmt-bb-pgmcc-02)>

## 2004 January ##
  * Juniper release JUNOS 7.2 with Router-Assist <[PGM Router Assist](http://www.m40.net/techpubs/software/junos/junos62/swconfig62-multicast/html/pgm-overview.html)>

## 2004 July ##
  * Draft 3 of PGMCC, a single rate multicast congestion control scheme <[draft-ietf-rmt-bb-pgmcc-03](http://www.tools.ietf.org/html/draft-ietf-rmt-bb-pgmcc-03)>

## 2005 May ##
  * Juniper Networks contributes PGM packet dumping to <tt>tcpdump</tt>.

## 2005 October ##
  * IBM release WebSphere Message Broker V6 with PGM/IP, and PGM UDP implementations <[Publish/subscribe](http://www.ibm.com/developerworks/websphere/library/techarticles/0510_dunn/0510_dunn.html)>

## 2006 August ##
  * Start of OpenPGM project.

## 2009 May ##
  * General availability of OpenPGM version 1.0.

## 2010 February ##
  * General availability of OpenPGM version 2.0 introducing full support of IPv6 and the Microsoft Windows platform.

## 2010 March ##
  * Start of <[LKML](http://lkml.org/)> discussion for integrating PGM into the Linux kernel.

## 2010 April ##
  * General availability of OpenPGM version 2.1 introducing full support of the Solaris operating system on SPARC hardware.
  * General availability of OpenPGM version 3.0, removing the dependency upon the GLib library thereby reducing the complexity of integrating upon multiple platforms.

## 2010 September ##
  * General availability of OpenPGM version 5.0 introducing a new BSD-socket styled API.
#summary OpenPGM 5 : C Reference : Socket
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
### Introduction ###
PGM Sockets manage network connections and send outbound messages.

### Topics in Alphabetical Order ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function or Type</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Protocols</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgm.md'>pgm</a></tt>(7)</td>
<td>PGM protocol.</td>
</tr><tr>
<td><h3>GSI API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiT.md'>pgm_gsi_t</a></tt></td>
<td>Object representing a GSI.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt></td>
<td>Create a GSI based on IPv4 host address.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt></td>
<td>Create a GSI based on MD5 of system host name.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiCreateFromString.md'>pgm_gsi_create_from_string()</a></tt></td>
<td>Create a GSI based on MD5 of a text string.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiCreateFromData.md'>pgm_gsi_create_from_data()</a></tt></td>
<td>Create a GSI based on MD5 of a data buffer.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiPrint.md'>pgm_gsi_print()</a></tt></td>
<td>Display a GSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiPrint.md'>pgm_gsi_print_r()</a></tt></td>
<td>Display a GSI in human friendly form, re-entrant form.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGsiEqual.md'>pgm_gsi_equal()</a></tt></td>
<td>Compare two GSI values.</td>
</tr><tr>
<td><h3>TSI API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmTsiT.md'>pgm_tsi_t</a></tt></td>
<td>Object representing a TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmTsiPrint.md'>pgm_tsi_print()</a></tt></td>
<td>Display a TSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmTsiPrint.md'>pgm_tsi_print_r()</a></tt></td>
<td>Display a TSI in human friendly form, re-entrant version.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmTsiEqual.md'>pgm_tsi_equal()</a></tt></td>
<td>Compare two TSI values.</td>
</tr><tr>
<td><h3>Socket API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmIoStatus.md'>PGMIOStatus</a></tt></td>
<td>Return status for PGM IO functions.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmIoVec.md'>pgm_iovec</a></tt></td>
<td>A scatter/gather message vector.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmMsgvT.md'>pgm_msgv_t</a></tt></td>
<td>A scatter/gather message vector<sup>2</sup>.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmFecInfoT.md'>pgm_fecinfo_t</a></tt></td>
<td>Settings for forward error correction (FEC).</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmPgmCcInfoT.md'>pgm_pgmccinfo_t</a></tt></td>
<td>Settings for PGM Congestion Control (PGMCC).</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt></td>
<td>A PGM socket object represents a delivery mechanism for messages.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSockAddrT.md'>pgm_sockaddr_t</a></tt></td>
<td>PGM endpoint address.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmRecv.md'>pgm_recv()</a></tt></td>
<td>Receive a message from the PGM socket and drive the PGM receive and send state machines.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmRecv.md'>pgm_recvfrom()</a></tt></td>
<td>Receive a message from the PGM socket saving source address.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmRecv.md'>pgm_recvmsg()</a></tt></td>
<td>Receive a message from the PGM socket with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmRecvMsgv.md'>pgm_recvmsgv()</a></tt></td>
<td>Receive a message vector from the PGM socket with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSend.md'>pgm_send()</a></tt></td>
<td>Send a message.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSendv.md'>pgm_sendv()</a></tt></td>
<td>Send a vector of messages.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSendSkbv.md'>pgm_send_skbv()</a></tt></td>
<td>Zero-copy send a vector of PGM skbuffs.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt></td>
<td>Bind a PGM socket to the specified network devices.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt></td>
<td>Initiate a PGM socket connection.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt></td>
<td>Create a PGM socket.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt></td>
<td>Close a PGM socket.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_getsockopt()</a></tt></td>
<td>Get a PGM socket option.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_setsockopt()</a></tt></td>
<td>Set a PGM socket option.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmGetSockName.md'>pgm_getsockname()</a></tt></td>
<td>Get the PGM socket name.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmSelectInfo.md'>pgm_select_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>select()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmPollInfo.md'>pgm_poll_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>poll()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgm5CReferencePgmEpollCtl.md'>pgm_epoll_ctl()</a></tt></td>
<td>Fill <tt>epoll_event</tt> parameters in preparation for <tt>epoll_wait()</tt>.</td>
</tr>
</table>
#summary OpenPGM 2 : C Reference : Transport
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
### Introduction ###
Transports manage network connections and send outbound messages.

### Topics in Alphabetical Order ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function or Type</th>
<th>Description</th>
</tr>
<tr>
<td><h3>GSI API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt></td>
<td>Object representing a GSI.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt></td>
<td>Create a GSI based on IPv4 host address.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt></td>
<td>Create a GSI based on MD5 of system host name.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiCreateFromString.md'>pgm_gsi_create_from_string()</a></tt></td>
<td>Create a GSI based on MD5 of a text string.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiCreateFromData.md'>pgm_gsi_create_from_data()</a></tt></td>
<td>Create a GSI based on MD5 of a data buffer.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiPrint.md'>pgm_gsi_print()</a></tt></td>
<td>Display a GSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiPrint.md'>pgm_gsi_print_r()</a></tt></td>
<td>Display a GSI in human friendly form, re-entrant form.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmGsiEqual.md'>pgm_gsi_equal()</a></tt></td>
<td>Compare two GSI values.</td>
</tr><tr>
<td><h3>TSI API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTsiT.md'>pgm_tsi_t</a></tt></td>
<td>Object representing a TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTsiPrint.md'>pgm_tsi_print()</a></tt></td>
<td>Display a TSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTsiPrint.md'>pgm_tsi_print_r()</a></tt></td>
<td>Display a TSI in human friendly form, re-entrant version.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTsiEqual.md'>pgm_tsi_equal()</a></tt></td>
<td>Compare two TSI values.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTsiEqual.md'>pgm_tsi_hash()</a></tt></td>
<td>Generate a TSI hash value.</td>
</tr><tr>
<td><h3>Transport API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmIoStatus.md'>PGMIOStatus</a></tt></td>
<td>Return status for PGM IO functions.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmIoVec.md'>pgm_iovec</a></tt></td>
<td>A scatter/gather message vector.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmMsgvT.md'>pgm_msgv_t</a></tt></td>
<td>A scatter/gather message vector<sup>2</sup>.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt></td>
<td>A transport object represents a delivery mechanism for messages.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmRecv.md'>pgm_recv()</a></tt></td>
<td>Receive a message from the transport.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmRecv.md'>pgm_recvfrom()</a></tt></td>
<td>Receive a message from the transport saving source TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmRecv.md'>pgm_recvmsg()</a></tt></td>
<td>Receive a message from the transport with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmRecvMsgv.md'>pgm_recvmsgv()</a></tt></td>
<td>Receive a message vector from the transport with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmSend.md'>pgm_send()</a></tt></td>
<td>Send a message.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmSendv.md'>pgm_sendv()</a></tt></td>
<td>Send a vector of messages.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmSendSkbv.md'>pgm_send_skbv()</a></tt></td>
<td>Zero-copy send a vector of PGM skbuffs.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt></td>
<td>Bind a transport to the specified network devices.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt></td>
<td>Create a network transport.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt></td>
<td>Destroy a transport.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetRateRemaining.md'>pgm_transport_get_rate_remaining()</a></tt></td>
<td>Get remaining time slice of the send rate limit engine.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetTimerPending.md'>pgm_transport_get_timer_pending()</a></tt></td>
<td>Get time before next timer event.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetRecvFd.md'>pgm_transport_get_recv_fd()</a></tt></td>
<td>Get receive event notification file descriptor.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetRecvFd.md'>pgm_transport_get_pending_fd()</a></tt></td>
<td>Get pending data event notification file descriptor.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetRecvFd.md'>pgm_transport_get_repair_fd()</a></tt></td>
<td>Get repair event notification file descriptor.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportGetRecvFd.md'>pgm_transport_get_send_fd()</a></tt></td>
<td>Get send event notification file descriptor.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>select()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportPollInfo.md'>pgm_transport_poll_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>poll()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt></td>
<td>Fill <tt>epoll_event</tt> parameters in preparation for <tt>epoll_wait()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportMaxTsdu.md'>pgm_transport_max_tsdu()</a></tt></td>
<td>Get maximum TSDU, or packet payload size with or without fragmentation.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt></td>
<td>Set interval of background SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt></td>
<td>Set intervals of data flushing SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetAbortOnReset.md'>pgm_transport_set_abort_on_reset()</a></tt></td>
<td>Close transport after detecting unrecoverable data loss.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetFec.md'>pgm_transport_set_fec()</a></tt></td>
<td>Set and enable Forward Error Correction parameters.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetMulticastLoop.md'>pgm_transport_set_multicast_loop()</a></tt></td>
<td>Set multicast loop and socket address sharing.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNonBlocking.md'>pgm_transport_set_nonblocking()</a></tt></td>
<td>Set non-blocking send and receive transport.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt></td>
<td>Set retries for DATA packets after NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt></td>
<td>Set retries for DATA after NCF.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt></td>
<td>Set NAK transmit back-off interval.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt></td>
<td>Set timeout for receiving RDATA.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt></td>
<td>Set timeout before repeating NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetPeerExpiry.md'>pgm_transport_set_peer_expiry()</a></tt></td>
<td>Set timeout for removing a dead peer.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRcvBuf.md'>pgm_transport_set_rcvbuf()</a></tt></td>
<td>Set receive buffer size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt></td>
<td>Set receive window size by data rate.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt></td>
<td>Set receive window size in seconds.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt></td>
<td>Set receive window size in sequence numbers.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRecvOnly.md'>pgm_transport_set_recv_only()</a></tt></td>
<td>Set transport to receive-only mode, data packets will not be published.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetSendOnly.md'>pgm_transport_set_send_only()</a></tt></td>
<td>Set transport to send-only mode, data packets will not be read.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetSndBuf.md'>pgm_transport_set_sndbuf()</a></tt></td>
<td>Set send buffer size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt></td>
<td>Set expiration time of SPM Requests.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt></td>
<td>Set send window size by data rate.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt></td>
<td>Set send window size in seconds.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetTxwSqns.md'>pgm_transport_set_txw_sqns()</a></tt></td>
<td>Set send window size in sequence numbers.</td>
</tr><tr>
<td><h3>Source Specific Multicast API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportJoinGroup.md'>pgm_transport_join_group()</a></tt></td>
<td>Join a multicast group (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportJoinGroup.md'>pgm_transport_leave_group()</a></tt></td>
<td>Leave a multicast group (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt></td>
<td>Block packets from a specific source IP address (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportBlockSource.md'>pgm_transport_unblock_source()</a></tt></td>
<td>Re-allow packets from a specific source IP address (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportJoinSourceGroup.md'>pgm_transport_join_source_group()</a></tt></td>
<td>Join a multicast group sent by a specific source IP address (SSM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportJoinSourceGroup.md'>pgm_transport_leave_source_group()</a></tt></td>
<td>Leave a multicast group sent by a specific source IP address (SSM).</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportMsFilter.md'>pgm_transport_msfilter()</a></tt></td>
<td>Block or re-allow packets from a list of source IP addresses.</td>
</tr>
</table>

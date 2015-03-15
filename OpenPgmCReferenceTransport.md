#summary OpenPGM : C Reference : Transport
#labels Phase-Implementation
#sidebar TOCCReference

### Introduction ###
Transports manage network connections and send outbound messages.

### Topics in Alphabetical Order ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function or Type</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Asynchronous receiver API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncT.md'>pgm_async_t</a></tt></td>
<td>Object representing a transport asynchronous receiver.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmEventFnT.md'>pgm_eventfn_t</a></tt></td>
<td>Callback function pointer for asynchronous events.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt></td>
<td>Create an asynchronous event handler.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt></td>
<td>Destroy an asynchronous event handler.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncGetFd.md'>pgm_async_get_fd()</a></tt></td>
<td>Retrieve file descriptor for event signalling.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncAddWatch.md'>pgm_async_add_watch()</a></tt></td>
<td>Add a transport event listener.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncAddWatch.md'>pgm_async_add_watch_full()</a></tt></td>
<td>Add a transport event listener, and run a completion function when all of the destroyed event’s callback functions are complete.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncCreateWatch.md'>pgm_async_create_watch()</a></tt></td>
<td>Create a transport event listener.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmAsyncRecv.md'>pgm_async_recv()</a></tt></td>
<td>Synchronous receiving from an asynchronous queue.</td>
</tr><tr>
<td><h3>GSI API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt></td>
<td>Object representing a GSI.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt></td>
<td>Create a GSI based on IPv4 host address.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmCreateMD5Gsi.md'>pgm_create_md5_gsi()</a></tt></td>
<td>Create a GSI based on MD5 of system host name.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmCreateStrGsi.md'>pgm_create_str_gsi()</a></tt></td>
<td>Create a GSI based on MD5 of provided string.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmCreateDataGsi.md'>pgm_create_data_gsi()</a></tt></td>
<td>Create a GSI based on MD5 of provided data buffer.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmPrintGsi.md'>pgm_print_gsi()</a></tt></td>
<td>Display a GSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmGsiEqual.md'>pgm_gsi_equal()</a></tt></td>
<td>Compare two GSI values.</td>
</tr><tr>
<td><h3>Interface API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmIfParseTransport.md'>pgm_if_parse_transport()</a></tt></td>
<td>Decompose a string network specification.</td>
</tr><tr>
<td><h3>Transport API</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTsiT.md'>pgm_tsi_t</a></tt></td>
<td>Object representing a TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmPrintTsi.md'>pgm_print_tsi()</a></tt></td>
<td>Display a TSI in human friendly form.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTsiEqual.md'>pgm_tsi_equal(</a>]</tt></td>
<td>Compare two TSI values.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmMsgvT.md'>pgm_msgv_t</a></tt></td>
<td>A scatter/gather message vector.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmSockErrT.md'>pgm_sock_err_t</a></tt></td>
<td>A structure detailing unrecoverable data loss.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt></td>
<td>A transport object represents a delivery mechanism for messages.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt></td>
<td>Bind a transport to the specified network devices.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt></td>
<td>Create a network transport.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt></td>
<td>Destroy a transport.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportRecv.md'>pgm_transport_recv()</a></tt></td>
<td>Receive a message from the transport.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportRecv.md'>pgm_transport_recvfrom()</a></tt></td>
<td>Receive a message from the transport saving source TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportRecv.md'>pgm_transport_recvmsg()</a></tt></td>
<td>Receive a message from the transport with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportRecvMsgv.md'>pgm_transport_recvmsgv()</a></tt></td>
<td>Receive a message vector from the transport with scatter/gather vector buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>select()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportPollInfo.md'>pgm_transport_poll_info()</a></tt></td>
<td>Set parameters suitable for feeding into <tt>poll()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt></td>
<td>Fill <tt>epoll_event</tt> parameters in preparation for <tt>epoll_wait()</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt></td>
<td>Set interval of background SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt></td>
<td>Set intervals of data flushing SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetCloseOnFailure.md'>pgm_transport_set_close_on_failure()</a></tt></td>
<td>Close transport after detecting unrecoverable data loss.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetFec.md'>pgm_transport_set_fec()</a></tt></td>
<td>Set and enable Forward Error Correction parameters.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetMulticastLoop.md'>pgm_transport_set_multicast_loop()</a></tt></td>
<td>Set multicast loop and socket address sharing.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt></td>
<td>Set retries for DATA packets after NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt></td>
<td>Set retries for DATA after NCF.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt></td>
<td>Set NAK transmit back-off interval.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt></td>
<td>Set timeout for receiving RDATA.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt></td>
<td>Set timeout before repeating NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetPeerExpiry.md'>pgm_transport_set_peer_expiry()</a></tt></td>
<td>Set timeout for removing a dead peer.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRcvBuf.md'>pgm_transport_set_rcvbuf()</a></tt></td>
<td>Set receive buffer size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt></td>
<td>Set receive window size by data rate.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRxwPreallocate.md'>pgm_transport_set_rxw_preallocate()</a></tt></td>
<td>Preallocate memory for receive window.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt></td>
<td>Set receive window size in seconds.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt></td>
<td>Set receive window size in sequence numbers.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRecvOnly.md'>pgm_transport_set_recv_only()</a></tt></td>
<td>Set transport to receive-only mode, data packets will not be published.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetSendOnly.md'>pgm_transport_set_send_only()</a></tt></td>
<td>Set transport to send-only mode, data packets will not be read.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetSndBuf.md'>pgm_transport_set_sndbuf()</a></tt></td>
<td>Set send buffer size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt></td>
<td>Set expiration time of SPM Requests.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt></td>
<td>Set send window size by data rate.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetTxwPreallocate.md'>pgm_transport_set_txw_preallocate()</a></tt></td>
<td>Preallocate memory for transmit window.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt></td>
<td>Set send window size in seconds.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetTxwSqns.md'>pgm_transport_set_txw_sqns()</a></tt></td>
<td>Set send window size in sequence numbers.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportJoinGroup.md'>pgm_transport_join_group()</a></tt></td>
<td>Join a multicast group (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportJoinGroup.md'>pgm_transport_leave_group()</a></tt></td>
<td>Leave a multicast group (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt></td>
<td>Block packets from a specific source IP address (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportBlockSource.md'>pgm_transport_unblock_source()</a></tt></td>
<td>Re-allow packets from a specific source IP address (ASM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportJoinSourceGroup.md'>pgm_transport_join_source_group()</a></tt></td>
<td>Join a multicast group sent by a specific source IP address (SSM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportJoinSourceGroup.md'>pgm_transport_leave_source_group()</a></tt></td>
<td>Leave a multicast group sent by a specific source IP address (SSM).</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportMsFilter.md'>pgm_transport_msfilter()</a></tt></td>
<td>Block or re-allow packets from a list of source IP addresses.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSend.md'>pgm_transport_send()</a></tt></td>
<td>Send a message.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSendv.md'>pgm_transport_sendv()</a></tt></td>
<td>Send a vector of messages.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSendPacketv.md'>pgm_transport_send_packetv()</a></tt></td>
<td>Zero-copy send a vector of application buffers.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportMaxTsdu.md'>pgm_transport_max_tsdu()</a></tt></td>
<td>Get maximum TSDU, or packet payload size with or without fragmentation.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmPacketvAlloc.md'>pgm_packetv_alloc()</a></tt></td>
<td>Allocate memory from the transmit window.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmPacketvFree1.md'>pgm_packetv_free1()</a></tt></td>
<td>Return unused memory to the transmit window.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmDropSuperUser.md'>pgm_drop_superuser()</a></tt></td>
<td>Drop superuser privileges needed to create PGM protocol sockets.</td>
</tr><tr>
<td><s><tt>pgm_sock_mreq</tt></s></td>
<td>A socket object representing an interface and multicast group.</td>
</tr>
</table>

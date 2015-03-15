### Topics in Alphabetical Order ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Packet</th>
<th>Description</th>
</tr>
<tr>
<td><tt><a href='OpenPgmConceptsOdata.md'>ODATA</a></tt></td>
<td>Original data packet.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsRdata.md'>RDATA</a></tt></td>
<td>Repair data packet.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNak.md'>NAK</a></tt></td>
<td>Negative acknowledgement.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNnak.md'>NNAK</a></tt></td>
<td>N-NAK or null negative acknowledgement.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNcf.md'>NCF</a></tt></td>
<td>NAK confirmation.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSpm.md'>SPM</a></tt></td>
<td>Source path message.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSpmr.md'>SPMR</a></tt></td>
<td>SPM-Request.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsPoll.md'>POLL</a></tt></td>
<td>Poll request.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsPolr.md'>POLR</a></tt></td>
<td>Poll response.</td>
</tr>
</table>


<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>IHB_MIN</a></tt></td>
<td>Minimum interval of <tt>IHB_TMR</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>IHB_MAX</a></tt></td>
<td>Maximum interval of <tt>IHB_TMR</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>IHB_TMR</a></tt></td>
<td>Inter-data-packet heartbeat timer.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_DATA_RETRIES</a></tt></td>
<td>Maximum NAK requests while waiting for data.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_NCF_RETRIES</a></tt></td>
<td>Maximum NAK requests while waiting for a matching NCF.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_BO_IVL</a></tt></td>
<td>Maximum time period for backing off NAK transmission.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_ELIM_IVL</a></tt></td>
<td>Time period for network elements to discarding duplicate NAK requests.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_RB_IVL</a></tt></td>
<td>Random time period over <tt>NAK_BO_IVL</tt> whilst listening for matching NCFs or NAKs.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_RPT_IVL</a></tt></td>
<td>Time period whilst listening for matching NCF before recommencing NAK generation.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_RDATA_IVL</a></tt></td>
<td>Time period whilst listening for data before recommencing NAK generation.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_TSI</a></tt></td>
<td><tt>OD_TSI</tt> of the ODATA packet for which a repair is requested.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_SQN</a></tt></td>
<td><tt>OD_SQN</tt> of the ODATA packet for which a repair is requested.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_SRC</a></tt></td>
<td>The unicast NLA of the original source of the missing ODATA.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NAK_GRP</a></tt></td>
<td>The multicast group NLA.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NNAK_TSI</a></tt></td>
<td><tt>NAK_TSI</tt> of the corresponding re-redirected NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NNAK_SQN</a></tt></td>
<td><tt>NAK_SQN</tt> of the corresponding re-redirected NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NNAK_SRC</a></tt></td>
<td><tt>NAK_SRC</tt> of the corresponding re-redirected NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NNAK_GRP</a></tt></td>
<td><tt>NAK_GRP</tt> of the corresponding re-redirected NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NCF_TSI</a></tt></td>
<td><tt>NAK_TSI</tt> of the NAK being confirmed.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NCF_SQN</a></tt></td>
<td><tt>NAK_SQN</tt> of the NAK being confirmed.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NCF_SRC</a></tt></td>
<td><tt>NAK_SRC</tt> of the NAK being confirmed.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsNegativeAcknowledgements.md'>NCF_GRP</a></tt></td>
<td><tt>NAK_GRP</tt> of the NAK being confirmed.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>OD_TSI</a></tt></td>
<td>The globally unique source-assigned TSI.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>OD_TRAIL</a></tt></td>
<td>The trailing (or left) edge of the source's transmit window (<tt>TXW_TRAIL</tt>).</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>OD_SQN</a></tt></td>
<td>A sequence number assigned sequentially by the source in unit increments and scoped by <tt>OD_TSI</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_LENGTH</a></tt></td>
<td>Option's Length.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_FRAGMENT</a></tt></td>
<td>Fragmentation.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_JOIN</a></tt></td>
<td>Late Joining.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_REDIRECT</a></tt></td>
<td>Redirect.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_SYN</a></tt></td>
<td>Synchronisation.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_FIN</a></tt></td>
<td>Session termination.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_RST</a></tt></td>
<td>Session reset.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_PARITY_PRM</a></tt></td>
<td>Forward Error Correction Parameters.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_PARITY_GRP</a></tt></td>
<td>Forward Error Correction Group Number.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_CURR_TGSIZE</a></tt></td>
<td>Forward Error Correction Group Size.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_CR</a></tt></td>
<td>Congestion Report.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_CRQST</a></tt></td>
<td>Congestion Report Request.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_NAK_BO_IVL</a></tt></td>
<td>NAK Back-Off Interval.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_NAK_BO_RNG</a></tt></td>
<td>NAK Back-off Range.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_NBR_UNREACH</a></tt></td>
<td>Neighbour Unreachable.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_PATH_NLA</a></tt></td>
<td>Path NLA.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsOptionEncodings.md'>OPT_INVALID</a></tt></td>
<td>Option invalidated.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>RD_TSI</a></tt></td>
<td><tt>OD_TSI</tt> of the ODATA package for which this is a repair.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>RD_TRAIL</a></tt></td>
<td>The trailing (or left) edge of the source's transmit window (<tt>TXW_TRAIL</tt>).</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsDataPackets.md'>RD_SQN</a></tt></td>
<td><tt>OD_SQN</tt> of the ODATA packet for which this is a repair.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsReceiveWindow.md'>RXW_LEAD</a></tt></td>
<td>The leading (or right) edge of the receive window.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsReceiveWindow.md'>RXW_TRAIL</a></tt></td>
<td>The trailing (or left) edge of the receive window.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsReceiveWindow.md'>RXW_TRAIL_INIT</a></tt></td>
<td>Initial trailing edge of the receive window to restrict repair requests.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>SPM_TSI</a></tt></td>
<td>The source-assigned TSI for the session.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>SPM_SQN</a></tt></td>
<td>A sequence number assigned sequentially by the source in unit increments and scoped by <tt>SPM_TSI</tt>.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>SPM_TRAIL</a></tt></td>
<td>The trailing (or left) edge of the source's transmit window (<tt>TXW_TRAIL</tt>).</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>SPM_LEAD</a></tt></td>
<td>The leading (or right) edge of the source's transmit window (<tt>TXW_LEAD</tt>).</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsSourcePathMessages.md'>SPM_PATH</a></tt></td>
<td>The network-layer address (NLA) of the interface on the PGM network element on which the SPM is forwarded.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsTransitWindow.md'>TXW_LEAD</a></tt></td>
<td>The leading (or right) edge of the transmit window.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsTxwMaxRte.md'>TXW_MAX_RTE</a></tt></td>
<td>The maximum transmit rate in bytes/second of a source.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsTxwSecs.md'>TXW_SECS</a></tt></td>
<td>The amount of transmitted data in seconds a source retains for repair.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsTxwSqns.md'>TXW_SQNS</a></tt></td>
<td>The size of the transmit window in sequence numbers.</td>
</tr><tr>
<td><tt><a href='OpenPgmConceptsTransitWindow.md'>TXW_TRAIL</a></tt></td>
<td>The trailing (or left) edge of the transmit window.</td>
</tr>
</table>

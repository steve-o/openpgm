#summary OpenPGM C Reference : Set these Parameters
#labels Phase-Implementation

OpenPGM does not default with a set of PGM parameters as it is necessary to customize to your messaging platform requirements. OpenPGM applications need to include calls to the following functions before binding a transport with a call to <tt><a href='OpenPgmCReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt>


<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function Name</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Sending</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetTxwSqns.md'>pgm_transport_set_txw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgmCReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt> and <tt><a href='OpenPgmCReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt></td>
<td>Set send window size, optionally enabling rate regulation engine.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt></td>
<td>Set interval of background SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt></td>
<td>Set intervals of data flushing SPM packets.</td>
</tr><tr>
<td><h3>Receiving</h3></td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgmCReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt> and <tt><a href='OpenPgmCReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt></td>
<td>Set receive window size.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetPeerExpiry.md'>pgm_transport_set_peer_expiry()</a></tt></td>
<td>Set timeout for removing a dead peer.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt></td>
<td>Set expiration time of SPM Requests.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt></td>
<td>Set NAK transmit back-off interval.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt></td>
<td>Set timeout before repeating NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt></td>
<td>Set timeout for receiving RDATA.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt></td>
<td>Set retries for DATA packets after NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgmCReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt></td>
<td>Set retries for DATA after NCF.</td>
</tr>
</table>

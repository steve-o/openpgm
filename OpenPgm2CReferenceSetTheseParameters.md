#summary OpenPGM 2 C Reference : Set These Parameters
#labels Phase-Implementation
#sidebar TOC2CReferenceProgrammersChecklist
OpenPGM does not default with a set of PGM parameters as it is necessary to customize to your messaging platform requirements. OpenPGM applications need to include calls to the following functions before binding a transport with a call to <tt><a href='OpenPgm2CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt>


<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function Name</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Sending</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetTxwSqns.md'>pgm_transport_set_txw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgm2CReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt> and <tt><a href='OpenPgm2CReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt></td>
<td>Set send window size, optionally enabling rate regulation engine.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt></td>
<td>Set interval of background SPM packets.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt></td>
<td>Set intervals of data flushing SPM packets.</td>
</tr><tr>
<td><h3>Receiving</h3></td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt></td>
<td>Set maximum transport data unit size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgm2CReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt> and <tt><a href='OpenPgm2CReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt></td>
<td>Set receive window size.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt></td>
<td>Set maximum number of network hops to cross.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetPeerExpiry.md'>pgm_transport_set_peer_expiry()</a></tt></td>
<td>Set timeout for removing a dead peer.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt></td>
<td>Set expiration time of SPM Requests.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt></td>
<td>Set NAK transmit back-off interval.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt></td>
<td>Set timeout before repeating NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt></td>
<td>Set timeout for receiving RDATA.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt></td>
<td>Set retries for DATA packets after NAK.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt></td>
<td>Set retries for DATA after NCF.</td>
</tr>
</table>


## Hints ##
All the example code specifies the default values used by TIBCO Rendezvous, SmartPGM, and Microsoft where appropriate.  They are very general and lenient settings to work on MANs and computers with low resolution timers.  It is recommended to start with the Rendezvous values, as used in all the examples, and adjust to your requirements.  Here follows the basic steps to go through,

## 1:  Choose a suitable window size ##

<tt><a href='OpenPgm2CReferencePgmTransportSetTxwSqns.md'>pgm_transport_set_txw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgm2CReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt> and <br />
<tt><a href='OpenPgm2CReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt>.

<tt><a href='OpenPgm2CReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt>, or<br />
<tt><a href='OpenPgm2CReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt> and<br />
<tt><a href='OpenPgm2CReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt>.


This can be difficult, Rendezvous defaults to a 60s buffer which will cripple you machine if you publish fast.  Rendezvous can only specify the window size in seconds, with OpenPGM you can specify seconds + bandwidth or individual sequence numbers.  The goal is to have the lowest possible window size that keeps your applications happy.  Too large a window consumes extra memory and can harm your applications cache locality with stale buffers.


## 2:  Choose a maximum packet size or TPDU ##

<tt><a href='OpenPgm2CReferencePgmTransportSetMaxTpdu.md'>pgm_transport_set_max_tpdu()</a></tt>

The TPDU is the fundamental packet size, send and receive buffers are always allocated with this size.  Receiving packets larger than the set TPDU will truncate the data causing data loss.  Default size is 1,500 bytes, you might want to increase to 9,000 bytes for jumbograms, or reduce to smaller size if you application does not use the entire space.  Reducing the TPDU will reduce the total size of the window, improving cache locality and potentially increase messaging throughput.


## 3:  Dead peer expiration timeout ##

<tt><a href='OpenPgm2CReferencePgmTransportSetPeerExpiry.md'>pgm_transport_set_peer_expiry()</a></tt>

How long do you want to wait before dead peers are reaped?  Rendezvous defaults to 300s, if you have publishing applications frequently bouncing up and down this will adversely increase the resource usage in subscribing applications.  Set the value too low and brief network faults will disrupt all the transports bringing all the applications down.


## 4:  Hops or TTL ##

<tt><a href='OpenPgm2CReferencePgmTransportSetHops.md'>pgm_transport_set_hops()</a></tt>

How wide is you network?  By default Rendezvous uses a multicast hop limit of 16, you might wish to increase this for larger networks.


## 5:  Recovery latency, limiting broadcast storms and yappy clients ##


<tt><a href='OpenPgm2CReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt>

For most of the time your network is going to not have packet loss, however there is no hard and fast rule for what is acceptable packet loss.  On a very large distribution network you may see 5-10% packet loss for instance.  From the rate of packet loss and business requirements you should be able to determine a latency target for recovered packets.  Business requirements might include how fresh data can be considered, how long before data is stale and can be discarded.

So perfect conditions latency is going to be RTT/2, where RTT means round trip time, the time it takes to go from the source to the destination and back to the source.

Basic recovery is going to be RTT✕3/2 + <tt>NAK_BO_IVL</tt>.

Worst case recovery is RTT✕3/2 + <tt>NAK_BO_IVL</tt> + <tt>NAK_RPT_IVL</tt> ✕ <tt>NAK_NCF_RETRIES</tt> + <tt>NAK_RDATA_IVL</tt> ✕ <tt>NAK_DATA_RETRIES</tt>.

For example, this could be 100us, 50ms, 200s, although the window size is likely to be the lower limit.

The important factor when choosing times is also what the target architecture can reliably provide in terms of resolution, how many cores there are and the likelyhood of context switches.  Do you have a reliable TSC?  Do you have a HPET?  Is POSIX select wait 1-16ms resolution?


## 6:  End of stream notification ##

<tt><a href='OpenPgm2CReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt>

<tt><a href='OpenPgm2CReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt>

SPMs are used to keep multicast path in your routing infrastructure, to update clients with the lead sequence of the transmit window, to provide an address to request repairs.  Imagine sending three packets and the last is lost, when is the client aware of that loss?

Imagine late joins, when a client starts and receives packets mid-stream of a publisher.  When packet loss occurs, excluding FEC enabled broadcast-only transports, the client needs the address of the source to request a re-transmission.  Heartbeat SPMs are used to broadcast the source address to clients, SPM-Requests are used to request early transmission of a SPM.
#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_ambient\_spm()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_ambient_spm* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
unsigned                spm_ambient_interval<br>
);<br>
</pre>

### Purpose ###
Set interval of background SPM packets.

### Remarks ###
Ambient SPMs are driven by a count-down timer that expires regularly.  The transmission of ambient SPMs is independent of heartbeat SPMs or data packets.  This ambient transmission of SPMs is required to keep the distribution tree information in the network current and to allow new receivers to synchronise with the session.

If a subscriber joins a high speed publishing feed, at a publishing rate such that no heartbeat SPM packets are generated, the subscriber will have to wait for the next ambient SPM before it can request repair via NAKs.  Note that SPM Requests (SPMRs) may be used to solicit an SPM without having to wait for the next scheduled delivery.

Cisco routers have a _feature_ that will halt multicast forwarding unless a packet is generated every 120 seconds.  TIBCO Rendezvous avoids this in TRDP mode by periodically sending out a <tt>HOST.STATUS</tt> messages.  In PGM it is integrated as part of the protocol to create and maintain the distribution tree.

TIBCO Rendezvous daemons using PGM have an ambient SPM broadcast interval of 30 seconds.  Microsoft PGM sends ambient SPMs every 3 seconds.

### Parameters ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>spm_ambient_interval</tt></td>
<td>Ambient SPM broadcast interval in microseconds.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
Continually broadcast a SPM packet every 8 seconds.

```
 *pgm_transport_set_ambient_spm* (transport, 8192*1000);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetHeartbeatSpm.md'>pgm_transport_set_heartbeat_spm()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportSetSpmrExpiry.md'>pgm_transport_set_spmr_expiry()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
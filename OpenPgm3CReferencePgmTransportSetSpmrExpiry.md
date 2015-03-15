#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_spmr\_expiry()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_spmr_expiry* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
unsigned                spmr_expiry<br>
);<br>
</pre>

### Purpose ###
Set expiration time of SPM Requests.

### Remarks ###
If a subscriber joins a high speed publishing feed, at a publishing rate such that no heartbeat SPM packets are generated, the subscriber will have to wait for the next ambient SPM before it can request repair via NAKs. SPM Requests (SPMRs) may be used to solicit an SPM without having to wait for the next scheduled delivery.

Before unicasting a given SPMR, receivers must choose a random delay on <tt>SPMR_BO_IVL</tt> (~250ms) during which they listen for a multicast of an identical SPMR.  If a receiver does not see a matching multicast SPMR within its chosen random interval, it must first multicast its own SPMR to the group with a TTL of 1 before then unicasting its own SPMR to the source.  If a receiver does see a matching multicast SPMR within its chosen random interval, it must refrain from unicasting its SPMR and wait instead for the corresponding SPM.

The receiver must wait at least <tt>SPMR_SPM_IVL</tt> before attempting to repeat the SPMR by choosing another delay on <tt>SPMR_BO_IVL</tt> and repeating the procedure above.

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
<td><tt>spmr_expiry</tt></td>
<td>Expiration time in microseconds.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
Set SPMR expiration period to 250ms.

```
 pgm_transport_set_spmr_expiry (transport, 250*1000);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

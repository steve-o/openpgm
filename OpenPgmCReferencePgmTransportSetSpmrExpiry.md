#summary OpenPGM : C Reference : pgm\_transport\_set\_spmr\_expiry()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_spmr_expiry* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                spmr_expiry<br>
);<br>
</pre>

### Purpose ###
Set expiration time of SPM Requests.

### Remarks ###
If a subscriber joins a high speed publishing feed, at a publishing rate such that no heartbeat SPM packets are generated, the subscriber will have to wait for the next ambient SPM before it can request repair via NAKs. SPM Requests (SPMRs) may be used to solicit an SPM without having to wait for the next scheduled delivery.

Before unicasting a given SPMR, receivers must choose a random delay on SPMR\_BO\_IVL (~250ms) during which they listen for a multicast of an identical SPMR.  If a receiver does not see a matching multicast SPMR within its chosen random interval, it must first multicast its own SPMR to the group with a TTL of 1 before then unicasting its own SPMR to the source.  If a receiver does see a matching multicast SPMR within its chosen random interval, it must refrain from unicasting its SPMR and wait instead for the corresponding SPM.

The receiver must wait at least SPMR\_SPM\_IVL before attempting to repeat the SPMR by choosing another delay on SPMR\_BO\_IVL and repeating the procedure above.

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
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Set SPMR expiration period to 250ms.

```
 pgm_transport_set_spmr_expiry (transport, 250*1000);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

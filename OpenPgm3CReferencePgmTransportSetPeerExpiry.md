#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_peer\_expiry()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_peer_expiry* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
unsigned                peer_expiry<br>
);<br>
</pre>

### Purpose ###
Set timeout for removing a dead peer.

### Remarks ###
The absence of regular Source Path Messages (SPMs) indicates the failure of a network element or the PGM source.

TIBCO Rendezvous handles session timeouts based on <tt>HOST.STATUS</tt> broadcasts, independent of whether TRDP or PGM is used as the underlying network transport.  The host timeout interval is 300 seconds.

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
<td><tt>peer_expiry</tt></td>
<td>Timeout in microseconds.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
Set expiration timeout to five times the ambient SPM interval, in effect allowing up to five lost broadcasts.

```
 pgm_transport_set_peer_expiry (transport, 5*8192*1000);
```

Set expiration interval to 300 seconds.

```
 pgm_transport_set_peer_expiry (transport, 300*1000*1000);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetAmbientSpm.md'>pgm_transport_set_ambient_spm()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
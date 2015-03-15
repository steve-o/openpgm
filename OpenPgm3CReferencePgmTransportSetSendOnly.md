#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_send\_only()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_send_only* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
bool             send_only<br>
);<br>
</pre>

### Purpose ###
Set transport to send-only mode, data packets will not be read.

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
<td><tt>send_only</tt></td>
<td>Set send-only transport.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_send_only (transport, true);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetRecvOnly.md'>pgm_transport_set_recv_only()</a></tt><br>
</li><li><a href='OpenPgm3CReferencePgmTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

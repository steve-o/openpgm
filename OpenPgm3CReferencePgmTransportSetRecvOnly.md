#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_recv\_only()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_recv_only* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
bool             recv_only,<br>
bool             is_passive<br>
);<br>
</pre>

### Purpose ###
Set transport to receive-only mode, data packets will not be published.

### Remarks ###
A passive receiver will not publish any packets to the network or send to the sender's NLA.

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
<td><tt>recv_only</tt></td>
<td>Receive data only.</td>
</tr><tr>
<td><tt>is_passive</tt></td>
<td>Passive receiver.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_recv_only (transport, true, false);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetSendOnly.md'>pgm_transport_set_send_only()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
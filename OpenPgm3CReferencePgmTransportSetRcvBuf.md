#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_rcvbuf()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_rcvbuf* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
size_t                  size<br>
);<br>
</pre>

### Purpose ###
Set receive buffer size.

### Remarks ###
Changes the receiver socket buffer sizes from the system default (<tt>/proc/sys/net/core/rmem_default</tt> on Linux) up to a maximum (<tt>/proc/sys/net/core/rmem_max</tt>).

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
<td><tt>size</tt></td>
<td>Size of socket buffer in bytes.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_rcvbuf (transport, 131071);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetSndBuf.md'>pgm_transport_set_sndbuf()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

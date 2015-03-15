﻿#summary OpenPGM : C Reference : pgm\_transport\_set\_sndbuf()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_sndbuf* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
int                  size<br>
);<br>
</pre>

### Purpose ###
Set send buffer size.

### Remarks ###
Changes the send socket buffer sizes from the system default (<tt>/proc/sys/net/core/wmem_default</tt>) up to a maximum (<tt>/proc/sys/net/core/wmem_max</tt>).

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
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
```
 pgm_transport_set_sndbuf (transport, 131071);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRcvBuf.md'>pgm_transport_set_rcvbuf()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
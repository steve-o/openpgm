#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_join\_group()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_join_group* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*      transport,<br>
struct group_req*     gr,<br>
size_t                 len<br>
);<br>
<br>
bool *pgm_transport_leave_group* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*      transport,<br>
struct group_req*     gr,<br>
size_t                 len<br>
);<br>
</pre>

### Purpose ###
The <tt>pgm_transport_join_group</tt> function joins a multicast group.

The <tt>pgm_transport_leave_group</tt> function leaves a previously joined multicast group.

### Remarks ###
Use <tt><a href='OpenPgm3CReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt> to ignore specific sources publishing to the joined multicast group.

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
<td><tt>gr</tt></td>
<td>Multicast group with optional interface.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>gr</tt> in bytes.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.

### Example ###
Add group 239.192.0.2 on default interface to transport.

```
 struct group_req gr;
 memset (&gr, 0, sizeof(gr));
 ((struct sockaddr*)&gr)->gr_group.sa_family = AF_INET;
 ((struct sockaddr_in*)&gr)->gr_group.sin_addr.s_addr = inet_addr("239.192.0.2");
 pgm_transport_join_group (transport, &gr, sizeof(gr));
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
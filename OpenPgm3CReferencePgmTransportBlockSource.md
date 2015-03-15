#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_block\_source()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_block_source* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*             transport,<br>
struct group_source_req*     gsr,<br>
socklen_t                        len<br>
);<br>
<br>
bool *pgm_transport_unblock_source* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*             transport,<br>
struct group_source_req*     gsr,<br>
socklen_t                        len<br>
);<br>
</pre>

### Purpose ###
The <tt>pgm_transport_block_source</tt> function turns off a given source.

The <tt>pgm_transport_unblock_source</tt> function re-allows a blocked source.

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
<td><tt>gsr</tt></td>
<td>Multicast group and source specification with optional interface.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>gr</tt> in bytes.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.

### Example ###
Blocks source 192.168.9.1, group 239.192.0.2 on default interface to transport.

```
 struct group_source_req gsr;
 memset (&gsr, 0, sizeof(gsr));
 ((struct sockaddr*)&gsr)->gsr_group.sa_family = AF_INET;
 ((struct sockaddr_in*)&gsr)->gsr_group.sin_addr.s_addr = inet_addr("239.192.0.2");
 ((struct sockaddr*)&gsr)->gsr_source.sa_family = AF_INET;
 ((struct sockaddr_in*)&gsr)->gsr_source.sin_addr.s_addr = inet_addr("192.168.9.1");
 pgm_transport_block_source (transport, &gsr, sizeof(gsr));
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportJoinGroup.md'>pgm_transport_join_group()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportJoinSourceGroup.md'>pgm_transport_join_source_group()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportMsFilter.md'>pgm_transport_msfilter()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
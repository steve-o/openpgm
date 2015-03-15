#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_join\_source\_group()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_join_source_group* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*            transport,<br>
struct group_source_req*     gsr,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                        len<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_leave_source_group* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*            transport,<br>
struct group_source_req*     gsr,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                        len<br>
);<br>
</pre>

### Purpose ###
The <tt>pgm_transport_join_source_group</tt> function joins a multicast source/group pair.

The <tt>pgm_transport_leave_source_group</tt> function leaves a previously joined multicast source/group pair.

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
<td>Multicast group and source with optional interface.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>gr</tt> in bytes.</td>
</tr>
</table>

### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.

### Example ###
Add group 239.192.0.2 from source 192.168.9.1 on default interface to transport.

```
 struct group_source_req gsr;
 memset (&sgr, 0, sizeof(gsr));
 ((struct sockaddr*)&gsr)->gsr_group.sa_family = AF_INET;
 ((struct sockaddr_in*)&gsr)->gsr_group.sin_addr.s_addr = inet_addr("239.192.0.2");
 ((struct sockaddr*)&gsr)->gsr_source.sa_family = AF_INET;
 ((struct sockaddr_in*)&gsr)->gsr_source.sin_addr.s_addr = inet_addr("192.168.9.1");
 pgm_transport_join_source_group (transport, &gsr, sizeof(gsr));
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportJoinGroup.md'>pgm_transport_join_group()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
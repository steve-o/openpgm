_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer] *pgm_packetv_alloc* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             can_fragment<br>
);<br>
</pre>

### Purpose ###
Allocate a packet from the transmit window, offset to the payload.

### Remarks ###
The memory buffer is owned by the transmit window and so must be returned to the window with content via [pgm\_transport\_send\_packetv()](OpenPgmCReferencePgmTransportSendPacketv.md) calls or unused with [pgm\_packetv\_free1()](OpenPgmCReferencePgmPacketvFree1.md).

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
<td><tt>can_fragment</tt></td>
<td>Whether this packet will be part of a multi-packet APDU.</td>
</tr>
</table>


### Return Value ###
On success, a pointer to the allocated buffer is returned.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmPacketvFree1.md'>pgm_packetv_free1()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportSendPacketv.md'>pgm_transport_send_packetv()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
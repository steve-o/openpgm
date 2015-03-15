_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_packetv_free1* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]             tsdu,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             can_fragment<br>
);<br>
</pre>

### Purpose ###
Return an unused packet allocated from the transmit window via <tt><a href='OpenPgmCReferencePgmPacketvAlloc.md'>pgm_packetv_alloc()</a></tt>.

### Remarks ###
The same <tt>can_fragment</tt> parameter value must be used as when the pointer was retrieved from the transmit window.

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
<td><tt>tsdu</tt></td>
<td>Packet buffer pointer.</td>
</tr><tr>
<td><tt>can_fragment</tt></td>
<td>Whether this packet will be part of a multi-packet APDU.</td>
</tr>
</table>


### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmPacketvAlloc.md'>pgm_packetv_alloc()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
#summary OpenPGM : C Reference : pgm\_transport\_max\_tsdu()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize] *pgm_transport_max_tsdu* (<br>
[OpenPgmCReferencePgmTransportT pgm_tranport_t]*    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]           can_fragment<br>
);<br>
</pre>

### Purpose ###
Get maximum TSDU, or packet payload size with or without fragmentation.

### Remarks ###
The underlying transport handles packets sized to the maximum transmission protocol data unit (TPDU), when an application provides a larger APDU it will fragment the message into many packets which the receivers will reconstruct and present as a single data unit.  The maximum size available per packet for application payload varies depending whether it is part of a larger APDU.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>PGM Transport.</td>
</tr><tr>
<td><tt>can_fragment</tt></td>
<td>Fragmented or non-fragmented packet.</td>
</tr>
</table>


### Return Value ###
Returns the maximum TSDU size, or number of bytes available per packet as application payload.

### Example ###
Send one zero-copy packet sized APDU.

```
 struct iovec vector = {
     .iov_base = pgm_packetv_alloc (transport, FALSE),
     .iov_len  = pgm_transport_max_tsdu (transport, FALSE)
 }
 pgm_transport_send_packetv (transport, vector, 1, 0, TRUE);
```

Send a 10 KB zero-copy sized APDU.

```
 gsize apdu_size = 10 * 1024;
 gsize tsdu_size = pgm_transport_max_tsdu (transport, TRUE);
 guint packets = apdu_size / tsdu_size;
 struct iovec vector[packets];
 for (guint i = 0; i < packets; i++) {
   vector[i].iov_base = pgm_packetv_alloc (transport, TRUE);
   vector[i].iov_len  = (i == (packets - 1)) ? (apdu_size % tsdu_size) : tsdu_size;
 }
 pgm_transport_send_packetv (transport, vector, packets, 0, TRUE);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmPacketvAlloc.md'>pgm_packetv_alloc()</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSendPacketv.md'>pgm_transport_send_packetv()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
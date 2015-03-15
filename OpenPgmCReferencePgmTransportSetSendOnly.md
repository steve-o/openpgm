#summary OpenPGM : C Reference : pgm\_transport\_set\_send\_only()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_send_only* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             send_only<br>
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
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
```
 pgm_transport_set_send_only (transport, TRUE);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRecvOnly.md'>pgm_transport_set_recv_only()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

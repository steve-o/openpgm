#summary OpenPGM : C Reference : pgm\_transport\_set\_fec()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_fec* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             use_proactive_parity,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             use_ondemand_parity,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             use_varpkt_len,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                default_n,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                default_k<br>
);<br>
</pre>

### Purpose ###
Set and enable Forward Error Correction parameters.

### Remarks ###
To send parity encoded APDUs you will most likely require variable length packets and so must set the <tt>use_varpkt_len</tt> parameter to <tt>TRUE</tt>.

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
<td><tt>use_proactive_parity</tt></td>
<td>Enable proactive parity generation.</td>
</tr><tr>
<td><tt>use_ondemand_parity</tt></td>
<td>Enable on-demand (reactive) parity generation.</td>
</tr><tr>
<td><tt>use_varpkt_len</tt></td>
<td>Packet sizes will vary within a transmission block.</td>
</tr><tr>
<td><tt>default_n</tt></td>
<td>Transmission group size.</td>
</tr><tr>
<td><tt>default_k</tt></td>
<td>Transmission block size.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
```
 pgm_transport_set_fec (transport, FALSE, TRUE, TRUE, 255, 4);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
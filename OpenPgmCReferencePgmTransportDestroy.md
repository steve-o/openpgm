#summary OpenPGM : C Reference : pgm\_transport\_destroy()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_destroy* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]* const    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]                  flush<br>
);<br>
</pre>

### Purpose ###
Destroy a transport.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  If <tt>transport</tt> invalid, returns <tt>-EINVAL</tt>.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
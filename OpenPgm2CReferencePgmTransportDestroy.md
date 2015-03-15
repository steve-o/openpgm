#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_destroy()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_destroy* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
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
</tr><tr>
<td><tt>flush</tt></td>
<td>Flush out channel with SPMs.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt>.  On invalid parameter, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt>.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
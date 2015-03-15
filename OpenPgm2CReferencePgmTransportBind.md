#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_bind()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_bind* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**                  error<br>
);<br>
</pre>

### Purpose ###
Bind a transport to the specified network devices.

### Remarks ###
Assigns local addresses to the PGM transport sockets which will initiate delivery of reconstructed messages to the asynchronous queue and its associated dispatcher threads.

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
<td><tt>error</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>

### Return Value ###
On success, <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt> is returned.  On failure, <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
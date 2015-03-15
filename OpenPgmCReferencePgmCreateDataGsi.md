#summary OpenPGM : C Reference : pgm\_create\_data\_gsi()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_create_data_gsi* (<br>
[OpenPgmCReferencePgmGsiT pgm_gsi_t]*             gsi,<br>
const unsigned char*   buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                  length,<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on MD5 of provided data buffer.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr><td>
<td><tt>buf</tt></td>
<td>Data buffer.</td>
<br>
<br>
Unknown end tag for </tr><br>
<br>
<tr>
<td><tt>length</tt></td>
<td>Length of <tt>buf</tt>.</td>
</tr>
<br>
<br>
Unknown end tag for </table><br>
<br>
<br>
<br>
<br>
<h3>Return Value</h3>
On success, 0 is returned.  If <tt>gsi</tt> is an invalid address, <tt>-EINVAL</tt> is returned.<br>
<br>
<h3>Example</h3>
<pre><code> pgm_gsi_t gsi;<br>
 unsigned char buf[] = { 0x1, 0x2, 0x3, 0x4 };<br>
 pgm_create_data_gsi (&amp;gsi, buf, sizeof(buf));<br>
</code></pre>

<h3>See Also</h3>
<ul><li><tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
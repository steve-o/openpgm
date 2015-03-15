#summary OpenPGM : C Reference : pgm\_print\_gsi()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
#define PGM_GSISTRLEN	 (sizeof("000.000.000.000.000.000"))<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_print_gsi* (<br>
const [OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_print_gsi_r* (<br>
const [OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi,<br>
char*               buf,<br>
size_t              bufsize<br>
);<br>
</pre>

### Purpose ###
Display a GSI in human friendly form.

### Remarks ###
<tt>pgm_print_gsi_r</tt> returns the same identifier in the array <tt>buf</tt> of size <tt>bufsize</tt>.

<tt>bufsize</tt> should be at least <tt>PGM_GSISTRLEN</tt> in length.


### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>Globally unique session identifier (GSI).</td>
</tr><tr>
<td><tt>buf</tt></td>
<td>Buffer to store GSI.</td>
</tr><tr>
<td><tt>bufsize</tt></td>
<td>Length of buffer <tt>buf</tt>.</td>
</tr>
</table>


### Return Value ###
On success, <tt>pgm_print_gsi</tt> returns a pointer to the GSI, and <tt>NULL</tt> on failure.  <tt>pgm_print_gsi_r</tt> returns 0 when successful, and <tt>-EINVAL</tt> on failure.

### Example ###
Display MD5 and IPv4 versions of GSI for the host.

```
 pgm_gsi_t gsi;
 pgm_create_md5_gsi(&gsi);
 printf ("MD5 GSI: %s\n", pgm_print_gsi (&gsi));
 pgm_create_ipv4_gsi(&gsi);
 printf ("IPv4 GSI: %s\n", pgm_print_gsi (&gsi));
```

Display MD5 GSI using re-entrant version <tt>pgm_print_gsi_r</tt>.

```
 pgm_gsi_t gsi;
 char buf[PGM_GSISTRLEN];
 pgm_create_md5_gsi (&gsi);
 pgm_print_gsi_r (&gsi, buf, sizeof(buf));
 printf ("MD5 GSI: %s\n", buf);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmCreateMD5Gsi.md'>pgm_create_md5_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
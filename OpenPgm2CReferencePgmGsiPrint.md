#summary OpenPGM 2 : C Reference : Transport : pgm\_gsi\_print()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
/* maximum length of GSI as a string */<br>
#define PGM_GSISTRLEN	 (sizeof("000.000.000.000.000.000"))<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_gsi_print* (<br>
const [OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_gsi_print_r* (<br>
const [OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi,<br>
char*               buf,<br>
size_t              bufsize<br>
);<br>
</pre>

### Purpose ###
Display a GSI in human friendly form.

### Remarks ###
<tt>pgm_gsi_print_r</tt> returns the same identifier in the array <tt>buf</tt> of size <tt>bufsize</tt>.

<tt>bufsize</tt> should be at least <tt>PGM_GSISTRLEN</tt> in length

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
On success, <tt>pgm_gsi_print</tt> returns a pointer to the GSI, and <tt>NULL</tt> on failure.  <tt>pgm_gsi_print_r</tt> returns 0 when successful, and <tt>-EINVAL</tt> on failure.

### Example ###
Display MD5 and IPv4 versions of GSI for the host.

```
 pgm_gsi_t gsi;
 pgm_gsi_create_from_hostname (&gsi, NULL);
 printf ("MD5 GSI: %s\n", pgm_gsi_print (&gsi));
 pgm_gsi_create_from_addr (&gsi, NULL);
 printf ("IPv4 GSI: %s\n", pgm_gsi_print (&gsi));
```

Display MD5 GSI using re-entrant version <tt>pgm_gsi_print_r</tt>.

```
 pgm_gsi_t gsi;
 char buf[PGM_GSISTRLEN];
 pgm_gsi_create_from_hostname (&gsi, NULL);
 pgm_gsi_print_r (&gsi, buf, sizeof(buf));
 printf ("MD5 GSI: %s\n", buf);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
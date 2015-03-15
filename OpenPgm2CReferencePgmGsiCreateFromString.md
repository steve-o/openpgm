#summary OpenPGM 2 C Reference : Transport : pgm\_gsi\_create\_from\_string()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_gsi_create_from_string* (<br>
[OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi,<br>
const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]*  str,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize]        length,<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on MD5 of provided string.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr><tr>
<td><tt>str</tt></td>
<td>Text string.</td>
</tr><tr>
<td><tt>length</tt></td>
<td>Length of <tt>str</tt>, or 0 for NULL terminated string.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.  If <tt>error</tt> is provided it may be set detailing the fault.

### Example ###
```
 pgm_gsi_t gsi;
 GError *err = NULL;
 if (!pgm_gsi_create_from_string (&gsi, "29 Acacia Road", 0)) {
   g_message ("GSI failed: %s", err->message);
   g_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
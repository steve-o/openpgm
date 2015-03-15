#summary OpenPGM 2 C Reference : Transport : pgm\_gsi\_create\_from\_data()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_gsi_create_from_data* (<br>
[OpenPgm2CReferencePgmGsiT pgm_gsi_t]*             gsi,<br>
const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guchar guchar]*          buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                  length<br>
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
</tr><tr>
<td><tt>buf</tt></td>
<td>Data buffer.</td>
</tr><tr>
<td><tt>length</tt></td>
<td>Length of <tt>buf</tt>.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.

### Example ###
```
 pgm_gsi_t gsi;
 unsigned char buf[] = { 0x1, 0x2, 0x3, 0x4 };
 if (!pgm_create_data_gsi (&gsi, buf, sizeof(buf))) {
   g_message ("GSI failed.");
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
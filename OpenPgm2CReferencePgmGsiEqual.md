#summary OpenPGM 2 : C Reference : Transport : pgm\_gsi\_equal()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gint gint] *pgm_gsi_equal* (<br>
const [OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi1,<br>
const [OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi2<br>
);<br>
</pre>

### Purpose ###
Compare two GSI values.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi1</tt></td>
<td>Globally unique session identifier (GSI).</td>
</tr><tr>
<td><tt>gsi2</tt></td>
<td>Globally unique session identifier (GSI).</td>
</tr>
</table>

### Return Value ###
The <tt>pgm_gsi_equal()</tt> function returns [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) if both values are equal and [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) otherwise.

### Example ###
```
 pgm_gsi_t gsi1, gsi2 = { .identifier = { 170, 29, 65, 155, 237, 36 } };
 pgm_create_md5_gsi(&gsi1);
 if (pgm_gsi_equal(&gsi1, &gsi2)) {
   g_message ("GSI matches host ayaka");
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
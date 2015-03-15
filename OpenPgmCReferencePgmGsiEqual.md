#summary OpenPGM : C Reference : pgm\_gsi\_equal()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gint gint] *pgm_gsi_equal* (<br>
const [OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi1,<br>
const [OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi2<br>
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
The <tt>pgm_gsi_equal()</tt> function returns TRUE if both values are equal and FALSE otherwise. .

### Example ###
```
 pgm_gsi_t gsi1, gsi2 = { .identifier = { 170, 29, 65, 155, 237, 36 } };
 pgm_create_md5_gsi(&gsi1);
 if (pgm_gsi_equal(&gsi1, &gsi2)) {
   g_message ("GSI matches host ayaka");
 }
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmCreateMD5Gsi.md'>pgm_create_md5_gsi()</a></tt>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
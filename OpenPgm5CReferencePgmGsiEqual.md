#summary OpenPGM 5 : C Reference : Socket : pgm\_gsi\_equal()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_gsi_equal* (<br>
const [OpenPgm5CReferencePgmGsiT pgm_gsi_t]*    gsi1,<br>
const [OpenPgm5CReferencePgmGsiT pgm_gsi_t]*    gsi2<br>
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
The <tt>pgm_gsi_equal()</tt> function returns <tt>true</tt> if both values are equal and <tt>false</tt> otherwise.

### Example ###
```
 pgm_gsi_t gsi1, gsi2 = { .identifier = { 170, 29, 65, 155, 237, 36 } };
 pgm_create_md5_gsi(&gsi1);
 if (pgm_gsi_equal(&gsi1, &gsi2)) {
   g_message ("GSI matches host ayaka");
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
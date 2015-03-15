#summary OpenPGM 2 : C Reference : Transport : pgm\_tsi\_equal()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gint gint] *pgm_tsi_equal* (<br>
const [OpenPgm2CReferencePgmTsiT pgm_tsi_t]*    tsi1,<br>
const [OpenPgm2CReferencePgmTsiT pgm_tsi_t]*    tsi2<br>
);<br>
</pre>

### Purpose ###
Compare two TSI values.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>tsi1</tt></td>
<td>Globally unique transport session identifier (TSI).</td>
</tr><tr>
<td><tt>tsi2</tt></td>
<td>Globally unique transport session identifier (TSI).</td>
</tr>
</table>

### Return Value ###
The <tt>pgm_tsi_equal()</tt> function returns [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) if both values are equal and [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) otherwise.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
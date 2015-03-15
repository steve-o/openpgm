#summary OpenPGM 5 : C Reference : Socket : pgm\_tsi\_equal()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_tsi_equal* (<br>
const void*    tsi1,<br>
const void*    tsi2<br>
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
The <tt>pgm_tsi_equal()</tt> function returns <tt>true</tt> if both values are equal and <tt>false</tt> otherwise.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
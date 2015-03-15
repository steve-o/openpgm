#summary OpenPGM 3 : C Reference : Error Handling : pgm\_error\_free()
#labels Phase-Implementation
#sidebar TOC3CReferenceErrorHandling
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
void *pgm_error_free* (<br>
[OpenPgm3CReferencePgmErrorT pgm_error_t]*    error<br>
);<br>
</pre>

### Purpose ###
Frees a <tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt> and associated resources.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>error</tt></td>
<td>A <tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt>.</td>
</tr>
</table>

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt><br>
<ul><li><a href='OpenPgm3CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.
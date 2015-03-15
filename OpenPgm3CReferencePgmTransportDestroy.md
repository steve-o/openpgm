#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_destroy()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_destroy* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
bool                  flush<br>
);<br>
</pre>

### Purpose ###
Destroy a transport.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>flush</tt></td>
<td>Flush out channel with SPMs.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
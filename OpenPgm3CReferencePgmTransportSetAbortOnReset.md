#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_abort\_on\_reset()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_abort_on_reset* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
bool             abort_on_reset<br>
);<br>
</pre>

### Purpose ###
Close transport after detecting unrecoverable data loss.

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
<td><tt>abort_on_reset</tt></td>
<td>Close transport on data-loss.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_abort_on_reset (transport, true);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
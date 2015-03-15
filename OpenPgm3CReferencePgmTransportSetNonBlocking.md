#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_nonblocking()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_nonblocking* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
bool             nonblocking<br>
);<br>
</pre>

### Purpose ###
Set non-blocking send and receive transport.

### Remarks ###
Conventional socket flag <tt>MSG_DONTWAIT</tt> doesn't work on Windows platforms so socket must be mode set into non-blocking operation.

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
<td><tt>nonblocking</tt></td>
<td>Enable non-blocking send and receive.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_nonblocking (transport, true);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
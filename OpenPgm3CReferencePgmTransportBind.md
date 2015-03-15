#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_bind()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_bind* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
[OpenPgm3CReferencePgmErrorT pgm_error_t]**                  error<br>
);<br>
</pre>

### Purpose ###
Bind a transport to the specified network devices.

### Remarks ###
Assigns local addresses to the PGM transport sockets which will initiate delivery of reconstructed messages to the asynchronous queue and its associated dispatcher threads.

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
<td><tt>error</tt></td>
<td>a return location for a <a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
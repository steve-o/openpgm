#summary OpenPGM : C Reference : pgm\_create\_md5\_gsi()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_create_md5_gsi* (<br>
[OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on MD5 of system host name.

### Remarks ###
A globally unique source identifier (GSI) is required for PGM to track the sending state of peers.  This function generates a GSI as suggested by the PGM draft specification using a MD5 hash of the system host name.

The GSI is specified when creating a new PGM transport with <tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt>.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr>
</table>


### Return Value ###
On success, 0 is returned.  If <tt>gsi</tt> is an invalid address, <tt>-EINVAL</tt> is returned.

### Example ###
```
 pgm_gsi_t gsi;
 pgm_create_md5_gsi (&gsi);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
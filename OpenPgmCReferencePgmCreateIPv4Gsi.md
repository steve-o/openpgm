#summary OpenPGM : C Reference : pgm\_create\_ipv4\_gsi()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_create_ipv4_gsi* (<br>
[OpenPgmCReferencePgmGsiT pgm_gsi_t]*    gsi<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on IPv4 host address.

### Remarks ###
A globally unique source identifier (GSI) is required for PGM to track the sending state of peers.  This function generates a GSI compatible with TIBCO Rendezvous which combines the four octets of the host IP address with a random number.  The random number is to help indicate if a process has been restarted as the transmit window is no longer valid.

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
On success, 0 is returned.  If <tt>gsi</tt> is invalid, <tt>-EINVAL</tt> is returned.

### Example ###
```
 pgm_gsi_t gsi;
 pgm_create_ipv4_gsi (&gsi);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmCreateMD5Gsi.md'>pgm_create_md5_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
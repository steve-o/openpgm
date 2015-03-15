#summary OpenPGM 3 : C Reference : Transport: pgm\_gsi\_create\_from\_addr()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_gsi_create_from_addr* (<br>
[OpenPgm3CReferencePgmGsiT pgm_gsi_t]*        gsi,<br>
[OpenPgm3CReferencePgmErrorT pgm_error_t]**     error<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on the nodes IPv4 address.

### Remarks ###
A globally unique source identifier (GSI) is required for PGM to track the sending state of peers.  This function generates a GSI compatible with TIBCO Rendezvous which combines the four octets of the host IP address with a random number.  The random number is to help indicate if a process has been restarted as the transmit window is no longer valid.

The GSI is specified when creating a new PGM transport with <tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt>.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.  If <tt>error</tt> is provided it may be set detailing the fault.

### Example ###
Basic usage without error handling.

```
 pgm_gsi_t gsi;
 pgm_gsi_create_from_addr (&gsi, NULL);
```

Display an error message detailing failure.

```
 pgm_gsi_t gsi;
 pgm_error_t *err = NULL;
 if (!pgm_gsi_create_from_addr (&gsi, &err)) {
   fprintf (stderr, "GSI failed: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
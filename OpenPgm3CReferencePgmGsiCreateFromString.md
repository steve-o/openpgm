#summary OpenPGM 3 C Reference : Transport : pgm\_gsi\_create\_from\_string()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_gsi_create_from_string* (<br>
[OpenPgm3CReferencePgmGsiT pgm_gsi_t]*    gsi,<br>
const char*   str,<br>
ssize_t       length,<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on MD5 of provided string.

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
<td><tt>str</tt></td>
<td>Text string.</td>
</tr><tr>
<td><tt>length</tt></td>
<td>Length of <tt>str</tt>, or 0 for NULL terminated string.</td>
</tr>
</table>


### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.  If <tt>error</tt> is provided it may be set detailing the fault.

### Example ###
```
 pgm_gsi_t gsi;
 pgm_error_t *err = NULL;
 if (!pgm_gsi_create_from_string (&gsi, "29 Acacia Road", 0)) {
   fprintf (stderr, "GSI failed: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm3CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.
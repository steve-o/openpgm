#summary OpenPGM 5 : C Reference : Socket : pgm\_gsi\_create\_from\_hostname()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_gsi_create_from_hostname* (<br>
[OpenPgm5CReferencePgmGsiT pgm_gsi_t]*        gsi,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**     error<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on the hostname.

### Remarks ###
A globally unique source identifier (GSI) is required for PGM to track the sending state of peers. This function generates a GSI as suggested by the PGM draft specification using a MD5 hash of the system host name.

The GSI is specified when binding a new PGM transport with <tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt>.

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
<td>a return location for a <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.  If <tt>error</tt> is provided it may be set detailing the fault.


### Errors ###

**PGM\_ERROR\_DOMAIN\_IF,**
  * _"Resolving hostname: %s"_
    * Platform specific error.


### Example ###
Basic usage without error handling.

```
 pgm_gsi_t gsi;
 pgm_gsi_create_from_hostname (&gsi, NULL);
```

Display an error message detailing failure.

```
 pgm_gsi_t gsi;
 pgm_error_t *err = NULL;
 if (!pgm_gsi_create_from_hostname (&gsi, &err)) {
   fprintf (stderr, "GSI failed: %s\n",
            (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
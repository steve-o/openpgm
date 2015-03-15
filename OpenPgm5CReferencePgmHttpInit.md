#summary OpenPGM 5 : C Reference : Monitoring and Administration : pgm\_http\_init()
#labels Phase-Implementation
#sidebar TOC5CReferenceMonitoringAndAdministration
_Function_
### Declaration ###
<pre>
#include <pgm/http.h><br>
<br>
#define PGM_HTTP_DEFAULT_SERVER_PORT    4968<br>
<br>
bool *pgm_http_init* (<br>
uint16_t        server_port,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**     error<br>
);<br>
</pre>

### Purpose ###
Create and start a HTTP administration interface.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>server_port</tt></td>
<td>HTTP port to listen on.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>


### Return Value ###
On success, <tt>TRUE</tt> is returned.  On error, <tt>FALSE</tt> is returned.  If <tt>err</tt> is provided it may be set detailing the fault.

### Example ###

```
 pgm_error_t* err = NULL;
 if (!pgm_http_init (PGM_HTTP_DEFAULT_SERVER_PORT, &err)) {
   fprintf ("Starting HTTP interface: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.
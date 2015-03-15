#summary OpenPGM : C Reference : pgm\_http\_init()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/http.h><br>
<br>
#define PGM_HTTP_DEFAULT_SERVER_PORT    4968<br>
<br>
int *pgm_http_init* (<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]        server_port<br>
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
</tr>
</table>


### Return Value ###
0 is returned.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><a href='OpenPgmCReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.
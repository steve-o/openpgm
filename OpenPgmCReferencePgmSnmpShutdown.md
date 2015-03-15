_Function_
### Declaration ###
<pre>
#include <pgm/snmp.h><br>
<br>
int *pgm_snmp_shutdown* (void);<br>
</pre>

### Purpose ###
Stop and destroy a running SNMP agent or master agent.

### Return Value ###
On success, 0 is returned.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
</li><li><a href='OpenPgmCReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://net-snmp.sourceforge.net/docs/readmefiles.html'>Net-SNMP Documentation</a>.
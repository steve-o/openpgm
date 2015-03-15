_Function_
### Declaration ###
<pre>
#include <pgm/snmp.h><br>
<br>
bool *pgm_snmp_shutdown* (void);<br>
</pre>

### Purpose ###
Stop and destroy a running SNMP agent or master agent.

### Return Value ###
On success, <tt>TRUE</tt> is returned.  On error, <tt>FALSE</tt> is returned.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmShutdown.md'>pgm_shutdown()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://net-snmp.sourceforge.net/docs/readmefiles.html'>Net-SNMP Documentation</a>.
_Function_
### Declaration ###
<pre>
#include <pgm/snmp.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_snmp_shutdown* (void);<br>
</pre>

### Purpose ###
Stop and destroy a running SNMP agent or master agent.

### Return Value ###
On success, <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt> is returned.  On error, <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt> is returned.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmShutdown.md'>pgm_shutdown()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://net-snmp.sourceforge.net/docs/readmefiles.html'>Net-SNMP Documentation</a>.
#summary OpenPGM : C Reference : pgm\_snmp\_init()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/snmp.h><br>
<br>
int *pgm_snmp_init* (void);<br>
</pre>

### Purpose ###
Create and start a SNMP agent or master agent.

### Remarks ###
The included PGM MIB <tt>PGM-MIB-petrova-01.txt</tt> should be installed in the NET-SNMP MIB directory, and the <tt>snmpd</tt> started and configured appropriately before calling this function.

Example <tt>snmpd.conf</tt>:
<pre>
com2sec readonly  default         public<br>
<br>
group MyROSystem v1        paranoid<br>
group MyROSystem v2c       paranoid<br>
group MyROSystem usm       paranoid<br>
group MyROGroup v1         readonly<br>
group MyROGroup v2c        readonly<br>
group MyROGroup usm        readonly<br>
group MyRWGroup v1         readwrite<br>
group MyRWGroup v2c        readwrite<br>
group MyRWGroup usm        readwrite<br>
<br>
view all    included  .1                               80<br>
view system included  .iso.org.dod.internet.mgmt.mib-2.system<br>
<br>
access MyROSystem ""     any       noauth    exact  system none   none<br>
access MyROGroup ""      any       noauth    exact  all    none   none<br>
access MyRWGroup ""      any       noauth    exact  all    all    none<br>
<br>
syslocation Unknown (configure /etc/snmp/snmpd.local.conf)<br>
syscontact Root <root@localhost> (configure /etc/snmp/snmpd.local.conf)<br>
<br>
master  agentx<br>
</pre>

### Return Value ###
On success, 0 is returned.  On error, 1 is returned, and a SNMP log entry created.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmSnmpShutdown.md'>pgm_snmp_shutdown()</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmHttpInit.md'>pgm_http_init()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><a href='OpenPgmCReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://net-snmp.sourceforge.net/docs/readmefiles.html'>Net-SNMP Documentation</a>.
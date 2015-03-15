#summary OpenPGM 5 : C Reference : Monitoring and Administration : pgm\_snmp\_init()
#labels Phase-Implementation
#sidebar TOC5CReferenceMonitoringAndAdministration
_Function_
### Declaration ###
<pre>
#include <pgm/snmp.h><br>
<br>
bool *pgm_snmp_init* (<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**     error<br>
);<br>
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

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>err</tt></td>
<td>a return location for a <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>


### Return Value ###
On success, <tt>TRUE</tt> is returned.  On error, <tt>FALSE</tt> is returned.  If <tt>err</tt> is provided it may be set detailing any threading or issue connecting to the SNMP agent or daemon.  SNMP errors will be reported to its log system.

### Example ###

```
 pgm_error_t* err = NULL;
 if (!pgm_snmp_init (&err)) {
   fprintf (stderr, "Starting SNMP interface: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSnmpShutdown.md'>pgm_snmp_shutdown()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmHttpInit.md'>pgm_http_init()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://net-snmp.sourceforge.net/docs/readmefiles.html'>Net-SNMP Documentation</a>.
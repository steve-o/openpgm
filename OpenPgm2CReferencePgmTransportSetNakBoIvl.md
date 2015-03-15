#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_set\_nak\_bo\_ivl()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_set_nak_bo_ivl* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                usec<br>
);<br>
</pre>

### Purpose ###
Set NAK transmit back-off interval.

### Remarks ###
Before transmitting a NAK, a receiver must wait some interval <tt>NAK_RB_IVL</tt> chosen randomly over some time period <tt>NAK_BO_IVL</tt>.  During this period, receipt of a matching NAK or a matching NCF will suspend NAK generation.  <tt>NAK_RB_IVL</tt> is counted down from the time a missing data packet is detected.  During <tt>NAK_RB_IVL</tt> a NAK is said to be pending.

The NAK back-off interval is a procedure intended to prevent NAK implosion and to limit its extent in the case of the loss of all or part of the suppressing multicast distribution tree.

SmartPGM uses a default of 500ms, Microsoft PGM uses a default of 600ms.

### Parameters ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>usec</tt></td>
<td>Interval in microseconds.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt>.  On invalid parameter, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt>.

### Example ###
Set <tt>NAK_BO_IVL</tt> to 50ms, same as TIBCO Rendezvous.

```
 pgm_transport_set_nak_bo_ivl (transport, 50*1000);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

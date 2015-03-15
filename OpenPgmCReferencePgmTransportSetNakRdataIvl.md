#summary OpenPGM : C Reference : pgm\_transport\_set\_nak\_rdata\_ivl()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_nak_rdata_ivl* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                usec<br>
);<br>
</pre>

### Purpose ###
Set timeout for receiving RDATA.

### Remarks ###
A receiver must suppress NAK generation and wait at least <tt>NAK_RDATA_IVL</tt> before recommencing NAK generation if it hears a matching NCF or NAK during <tt>NAK_RB_IVL</tt>.

SmartPGM uses a default of 2s, but can auto-tune from 20-2000ms depending on network conditions.  Microsoft PGM defaults to 4000ms.

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
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Set <tt>NAK_RDATA_IVL</tt> to 200ms.

```
 pgm_transport_set_nak_rdata_ivl (transport, 200*1000);
```

Set <tt>NAK_RDATA_IVL</tt> to 2 seconds, matching TIBCO Rendezvous over PGM.

```
 pgm_transport_set_nak_rpt_ivl (transport, 2*1000*1000);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportSetNakRptIvl.md'>pgm_transport_set_nak_rpt_ivl()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

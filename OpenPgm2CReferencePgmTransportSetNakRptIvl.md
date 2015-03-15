#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_set\_nak\_rpt\_ivl()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_set_nak_rpt_ivl* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                usec<br>
);<br>
</pre>

### Purpose ###
Set timeout before repeating NAK.

### Remarks ###
NAKs are repeated at a rate governed by <tt>NAK_RPT_IVL</tt> up to <tt>NAK_NCF_RETRIES</tt> times while waiting for a matching NCF.

SmartPGM uses a default of 500ms, but can auto-tune from 20-2000ms depending on network conditions.  Microsoft PGM defaults to 1000ms.

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
<td>AInterval in microseconds.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt>.  On invalid parameter, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt>.

### Example ###
Set <tt>NAK_RPT_IVL</tt> to 200ms.

```
 pgm_transport_set_nak_rpt_ivl (transport, 200*1000);
```

Set <tt>NAK_RPT_IVL</tt> to 2 seconds, matching TIBCO Rendezvous over PGM.

```
 pgm_transport_set_nak_rpt_ivl (transport, 2*1000*1000);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportSetNakBoIvl.md'>pgm_transport_set_nak_bo_ivl()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportSetNakRdataIvl.md'>pgm_transport_set_nak_rdata_ivl()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

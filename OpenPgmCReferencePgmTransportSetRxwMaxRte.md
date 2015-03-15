_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_rxw_max_rte* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                max_rte<br>
);<br>
</pre>

### Purpose ###
Set receive window size by data rate.

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
<td><tt>max_rte</tt></td>
<td>Maximum rate of bytes per second.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Create a receive window capable of storing 400,000 bytes per second for 10 seconds.

```
 pgm_transport_set_rxw_max_rte (transport, 400*1000);
 pgm_transport_set_rxw_secs (transport, 10);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRxwSecs.md'>pgm_transport_set_rxw_secs()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportSetTxwMaxRte.md'>pgm_transport_set_txw_max_rte()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
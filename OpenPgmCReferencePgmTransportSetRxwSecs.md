#summary OpenPGM : C Reference : pgm\_transport\_set\_rxw\_secs()
#labels Phase-Implementation
#sidebar TOCCReference
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_rxw_secs* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                secs<br>
);<br>
</pre>

### Purpose ###
Set receive window size in seconds.

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
<td><tt>secs</tt></td>
<td>Size of window in seconds.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Create a 10 second receive window with maximum data rate of 400,000 packets per second.

```
 pgm_transport_set_rxw_max_rte (transport, 400*1000);
 *pgm_transport_set_rxw_secs* (transport, 10);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRxwMaxRte.md'>pgm_transport_set_rxw_max_rte()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportSetTxwSecs.md'>pgm_transport_set_txw_secs()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
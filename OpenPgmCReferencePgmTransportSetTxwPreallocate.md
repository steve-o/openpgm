#summary OpenPGM : C Reference : pgm\_transport\_set\_txw\_preallocate()
#labels Phase-Implementation
#sidebar TOCCReference
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_txw_preallocate* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                sqns<br>
);<br>
</pre>

### Purpose ###
Preallocate memory for transmit window.

### Remarks ###
Memory is not freed from the transmit window until the transport has been destroyed.

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
<td><tt>sqns</tt></td>
<td>Number of entries.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
```
 pgm_transport_set_txw_preallocate (transport, 1000);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRxwPreallocate.md'>pgm_transport_set_rxw_preallocate()</a></tt></li></ul>

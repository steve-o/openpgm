#summary OpenPGM : C Reference : pgm\_transport\_set\_txw\_sqns()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_txw_sqns* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                sqns<br>
);<br>
</pre>

### Purpose ###
Set send window size in sequence numbers.

### Remarks ###
Microsoft PGM sets its transmt window size in bytes, a default size of 10 MB which is roughly 7,000 sequence numbers when using 1,500 MTU sized IPv4 packets.  Microsoft PGM stores its window contents on disk to enable large window sizes, OpenPGM only stores in memory.

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
<td>Size of window in sequence numbers.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Create a reliability window sufficient to store 400,000 packets.

```
 pgm_transport_set_txw_sqns (transport, 400*1000);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetRxwSqns.md'>pgm_transport_set_rxw_sqns()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
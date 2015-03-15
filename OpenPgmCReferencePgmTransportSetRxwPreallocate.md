#summary OpenPGM : C Reference : pgm\_transport\_set\_rxw\_preallocate()
#labels Phase-Implementation
#sidebar TOCCReference
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_set_rxw_preallocate* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                sqns<br>
);<br>
</pre>

### Purpose ###
Preallocate memory for receive window.

### Remarks ###
The sequence numbers are allocated every time a receive window is created.  When multiple windows are created and destroy the internal trash stack of allocated packets will grow and not shrink until the transport is destroyed, i.e. pre-allocation appears as a memory leak when pears restart.

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
 pgm_transport_set_rxw_preallocate (transport, 1000);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetTxwPreallocate.md'>pgm_transport_set_txw_preallocate()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
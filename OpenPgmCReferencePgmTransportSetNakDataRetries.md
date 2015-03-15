#summary OpenPGM : C Reference : pgm\_transport\_set\_nak\_data\_retries()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_nak_data_retries* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                cnt<br>
);<br>
</pre>

### Purpose ###
Set retries for DATA packets after NAK.

### Remarks ###
NAK generation is cancelled upon the advancing of the receive window so as to exclude the matching sequence number of a pending or outstanding NAK, or <tt>NAK_DATA_RETRIES</tt>/<tt>NAK_NCF_RETRIES</tt> being exceeded.  Cancellation of NAK generation indicates unrecoverable data loss.

SmartPGM since release 4.0.1 uses a value of 50 retries but can auto-tune the setting to 20-100 depending on network conditions.  Microsoft PGM defaults to 10 retries.

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
<td><tt>cnt</tt></td>
<td>NAK packet retry count.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
Set <tt>NAK_DATA_RETRIES</tt> to 5.

```
 pgm_transport_set_nak_data_retries (transport, 5);
```

SmartPGM since release 4.0.1 uses a value of 50 retries.

```
 pgm_transport_set_nak_data_retries (transport, 50);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

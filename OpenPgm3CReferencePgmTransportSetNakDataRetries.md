#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_nak\_data\_retries()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_nak_data_retries* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
unsigned                cnt<br>
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
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

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
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSetNakNcfRetries.md'>pgm_transport_set_nak_ncf_retries()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

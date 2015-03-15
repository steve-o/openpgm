#summary OpenPGM 2 : C Reference : Transport : pgm\_transport\_set\_nak\_ncf\_retries()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_transport_set_nak_ncf_retries* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                cnt<br>
);<br>
</pre>

### Purpose ###
Set retries for DATA after NCF.

### Remarks ###
NAK generation is cancelled upon the advancing of the receive window so as to exclude the matching sequence number of a pending or outstanding NAK, or <tt>NAK_DATA_RETRIES</tt> / <tt>NAK_NCF_RETRIES</tt> being exceeded.  Cancellation of NAK generation indicates unrecoverable data loss.

SmartPGM uses a value of 50 retries but can auto-tune the setting to 10-100 depending on network conditions.  Microsoft PGM defaults to 10 retries.

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
<td>Maximum number of retries.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS'>TRUE</a></tt>.  On invalid parameter, returns <tt><a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS'>FALSE</a></tt>.

### Example ###
```
 pgm_transport_set_nak_ncf_retries (transport, 2);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportSetNakDataRetries.md'>pgm_transport_set_nak_data_retries()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

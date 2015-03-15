#summary OpenPGM : C Reference : pgm\_transport\_set\_multicast\_loop()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_set_multicast_loop* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*     transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]             use_multicast_loop<br>
);<br>
</pre>

### Purpose ###
Set multicast loop and socket address sharing.

### Remarks ###
PGM is a bi-direction communication protocol using the back channel for NAKs, by default multicast loop is therefore disabled to remove unwanted data.  The side effect of disabling multicast loop is that two applications on the same host cannot communicate with each other using PGM.  For production purposes it would be better to use a regular IPC method and it is not recommended, but for development of PGM applications multicast loop allows the convenience of development on one host.

'''1.0 only''': When using UDP-encapsulation this parameter also enables socket address sharing via <tt>SO_REUSEADDR</tt> to allow multiple applications to bind to the same UDP port.

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
<td><tt>use_multicast_loop</tt></td>
<td>Enable multicast loop and socket address sharing.</td>
</tr>
</table>


### Return Value ###
On success, returns 0.  On invalid parameter, returns <tt>-EINVAL</tt>.

### Example ###
```
 pgm_transport_set_multicast_loop (transport, TRUE);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
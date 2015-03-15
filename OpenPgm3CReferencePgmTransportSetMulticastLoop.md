#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_set\_multicast\_loop()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_set_multicast_loop* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*     transport,<br>
bool             use_multicast_loop<br>
);<br>
</pre>

### Purpose ###
Set multicast loop and socket address sharing.

### Remarks ###
PGM is a bi-direction communication protocol using the back channel for NAKs, by default multicast loop is therefore disabled to remove unwanted data.  The side effect of disabling multicast loop is that two applications on the same host cannot communicate with each other using PGM.  For production purposes it would be better to use a regular IPC method and it is not recommended, but for development of PGM applications multicast loop allows the convenience of development on one host.

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
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###
```
 pgm_transport_set_multicast_loop (transport, true);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
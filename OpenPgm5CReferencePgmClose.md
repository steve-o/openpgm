#summary OpenPGM 5 : C Reference : Socket : pgm\_close()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_close* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const    sock,<br>
bool                 flush<br>
);<br>
</pre>

### Purpose ###
Destroy a transport.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>sock</tt></td>
<td>The PGM socket object.</td>
</tr><tr>
<td><tt>flush</tt></td>
<td>Flush out channel with SPMs.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.


### Example ###
```
pgm_sock_t *sock;
pgm_close (sock, FALSE);
```
Displaying the following output.
```
Trace: Closing receive socket.
Trace: Closing send socket.
Trace: Destroying rate control.
Trace: Closing send with router alert socket.
```


### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
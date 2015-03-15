#summary OpenPGM 5 : C Reference : Socket : pgm\_connect()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_connect* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const    sock,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**        error<br>
);<br>
</pre>

### Purpose ###
Initiate a PGM socket connection.

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
<td><tt>error</tt></td>
<td>a return location for a <a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.


### Errors ###
The following are general socket errors only. There may be other platform-specific error codes.

**PGM\_ERROR\_DOMAIN\_SOCKET,**
  * _"Sending SPM broadcast: %s"_
    * Platform specific error during socket send call.


### Example ###
```
pgm_sock_t *sock = NULL;
pgm_error_t *err = NULL;
if (!pgm_connect (sock, &err)) {
  fprintf (stderr, "Connecting PGM socket: %s\n",
           (err && err->message) ? err->message : "(null)");
  pgm_error_free (err);
  return EXIT_FAILURE;
}
```


### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
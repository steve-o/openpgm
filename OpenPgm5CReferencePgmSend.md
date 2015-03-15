#summary OpenPGM 5 : C Reference : Socket : pgm\_send()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_send* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const sock,<br>
const void*       buf,<br>
size_t            len,<br>
size_t*           bytes_written<br>
);<br>
</pre>

### Purpose ###
Send an Application Protocol Data Unit (APDU) to the network.

### Remarks ###
The underlying transport handles packets sized to the maximum transmission protocol data unit (TPDU), when an application provides a larger APDU it will fragment the message into many packets which the receivers will reconstruct and present as a single data unit.

A would be blocked operation might have sent one or more transmission data units.  The context is saved internally but subsequent call should be either <tt>pgm_send()</tt> with the exact same parameters, or <tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt>.  Put another way: in a multi-threaded application a locking mechanism should be implemented to prevent another thread trying to send a different application data unit.

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
<td><tt>buf</tt></td>
<td>Data buffer to fill.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>buf</tt> in bytes.</td>
</tr><tr>
<td><tt>bytes_written</tt></td>
<td>Pointer to store count of bytes written from <tt>buf</tt>.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>PGM_IO_STATUS_NORMAL</tt>, on error returns <tt>PGM_IO_STATUS_ERROR</tt>.  If the transport is marked non-blocking then <tt>PGM_IO_STATUS_WOULD_BLOCK</tt> is returned if the operation would block, if the block would be caused by the rate limiting engine then <tt>PGM_IO_STATUS_RATE_LIMITED</tt> is returned instead.

If PGMCC is enabled and congestion occurs <tt>PGM_IO_STATUS_CONGESTION</tt> is returned, the application should wait on notification from the ACK channel before re-attempting to send.

### Example ###
Send the traditional "Hello world" with terminating NULL.

```
 char* s = "hello world!";
 size_t bytes_written;
 pgm_send (sock, s, strlen(s) + 1, &bytes_written);
```

Basic non-blocking send.

```
 char* s = "hello world!";
 size_t len = strlen (s) + 1;
 int status;
 do {
   status = pgm_send (sock, s, len, NULL);
 } while (PGM_IO_STATUS_WOULD_BLOCK == status ||
          PGM_IO_STATUS_RATE_LIMITED == status);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmIoStatus.md'>PGMIOStatus</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmSendv.md'>pgm_sendv()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
#summary OpenPGM 5 : C Reference : Socket : pgm\_select\_info()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_select_info* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const    sock,<br>
fd_set*              readfds,<br>
fd_set*              writefds,<br>
int*                 nfds<br>
);<br>
</pre>

### Purpose ###
Set parameters suitable for feeding into <tt>select()</tt>.

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
<td><tt>readfds</tt></td>
<td>File descriptors to be watched for reading.</td>
</tr><tr>
<td><tt>writefds</tt></td>
<td>File descriptors to be watched for writing.</td>
</tr><tr>
<td><tt>nfds</tt></td>
<td>Highest-numbered file descriptor plus 1.</td>
</tr>
</table>

### Return Value ###
On success, returns the highest-numbered file descriptor plus 1.  If the transport is closed, returns -1 and sets <tt>errno</tt> to <tt>EBADF</tt>.

### Example ###
Monitor the PGM transport for incoming events.

```
 int nfds = 0;
 fd_set readfds;
 FD_ZERO(&readfds);
 pgm_select_info (sock, &readfds, NULL, &nfds);
 nfds = select (nfds, &readfds, NULL, NULL, NULL);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmPollInfo.md'>pgm_poll_info()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmEpollCtl.md'>pgm_epoll_ctl()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
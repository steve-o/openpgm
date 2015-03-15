#summary OpenPGM 5 : C Reference : Socket : pgm\_poll\_info()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#define CONFIG_HAVE_POLL<br>
#include <pgm/pgm.h><br>
<br>
int *pgm_poll_info* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const    sock,<br>
struct pollfd*       fds,<br>
int*                 nfds,<br>
int                  events<br>
);<br>
</pre>

### Purpose ###
Set parameters suitable for feeding into <tt>poll()</tt> or <tt><a href='http://msdn.microsoft.com/en-us/library/ms741669(v=vs.85).aspx'>WSAPoll()</a></tt>.

### Remarks ###
An assertion will be raised if insufficient file descriptors are provided.  1 file descriptor is required for basic <tt>POLLIN</tt> events, another is required if the transport has a receive channel (default configuration, or setting <tt>PGM_SEND_ONLY</tt> with <tt>false</tt>).  1 file descriptor is required for basic <tt>POLLOUT</tt> events.

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
<td><tt>fds</tt></td>
<td>Array to store poll file descriptors.</td>
</tr><tr>
<td><tt>n_fds</tt></td>
<td>Length of <tt>fds</tt> array, and file descriptors used.</td>
</tr><tr>
<td><tt>events</tt></td>
<td>A bit set composed using the following available event types:<br>
<dl><dt><tt>POLLIN</tt></dt><dd>The transport is available for <tt>recv()</tt> operations.<br>
</dd><dt><tt>POLLOUT</tt></dt><dd>The transport is available for <tt>send()</tt> operations.</dd></dl></td>
</tr>
</table>

### Return Value ###
On success, returns the number of <tt>pollfd</tt> event structures filled.  If the transport is closed, returns -1 and sets <tt>errno</tt> to <tt>EBADF</tt>.

### Example ###
Poll the PGM transport for incoming events.

```
 int nfds = IP_MAX_MEMBERSHIPS;
 struct pollfd fds[ IP_MAX_MEMBERSHIPS ];
 memset (fds, 0, sizeof (fds));
 pgm_poll_info (sock, &fds, &nfds, POLLIN);
 poll (fds, nfds, 1000 /* ms */);
```

Using <tt>WSAPoll()</tt> for non-blocking send on Windows,

```
 ULONG nfds = PGM_BUS_SOCKET_WRITE_COUNT;
 WSAPOLLFD fds[ PGM_BUS_SOCKET_WRITE_COUNT ];
 ZeroMemory (fds, sizeof (WSAPOLLFD) * n_fds);
 pgm_poll_info (sock, &fds, &nfds, POLLOUT);
 WSAPoll (fds, n_fds, 1000 /* ms */);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSelectInfo.md'>pgm_select_info()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmEpollCtl.md'>pgm_epoll_ctl()</a></tt><br>
</li><li><tt><a href='http://msdn.microsoft.com/en-us/library/ms741669(v=vs.85).aspx'>WSAPoll()</a></tt> in MSDN Library.<br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
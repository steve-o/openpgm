#summary OpenPGM : C Reference : pgm\_transport\_poll\_info()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_poll_info* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]* const    transport,<br>
struct pollfd*            fds,<br>
int*                      nfds,<br>
int                       events<br>
);<br>
</pre>

### Purpose ###
Set parameters suitable for feeding into <tt>poll()</tt>.

### Remarks ###
An assertion will be raised if insufficient file descriptors are provided.  1 file descriptor is required for basic <tt>POLLIN</tt> events, another is required if the transport has a receive channel (default configuration, or calling <tt><a href='OpenPgmCReferencePgmTransportSetSendOnly.md'>pgm_transport_set_send_only()</a></tt> with <tt>FALSE</tt>).  1 file descriptor is required for basic <tt>POLLOUT</tt> events.

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
 memset (fds, 0, sizeof(fds));
 pgm_transport_poll_info (transport, &fds, &nfds, POLLIN);
 poll (fds, nfds, 1000 /* ms */);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
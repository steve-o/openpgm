#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_epoll\_ctl()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#define CONFIG_HAVE_EPOLL<br>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_epoll_ctl* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
int                       epfd,<br>
int                       op,<br>
int                       events<br>
);<br>
</pre>

### Purpose ###
Set parameters suitable for using with <tt>epoll_wait()</tt>.

### Remarks ###
An Edge Triggered event signals the transition to a rising edge (0 to 1).  In simple terms, the start of a series of incoming events.  Data should be read until <tt>EAGAIN</tt> is returned before the next Edge Triggered event will occur.

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
<td><tt>epfd</tt></td>
<td>epoll file descriptor.</td>
</tr><tr>
<td><tt>op</tt></td>
<td>Operation, must be <tt>EPOLL_CTL_ADD</tt>.</td>
</tr><tr>
<td><tt>events</tt></td>
<td>A bit set composed using the following available event types:<br>
<dl><dt><tt>EPOLLIN</tt></dt><dd>The transport is available for <tt>recv()</tt> operations.<br>
</dd><dt><tt>EPOLLOUT</tt></dt><dd>The transport is available for <tt>send()</tt> operations.<br>
</dd><dt><tt>EPOLLET</tt></dt><dd>Sets the Edge Triggered behaviour for the transport.  The default behaviour is Level Triggered.</dd></dl></td>
</tr>
</table>

### Return Value ###
On success, returns 0.  On error, returns -1 and <tt>errno</tt> is set appropriately.

### Errors ###
<dl><dt><tt>EBADF</tt></dt><dd><tt>epfd</tt> is not a valid file descriptor or the transport is closed.<br>
</dd><dt><tt>EEXIST</tt></dt><dd><tt>op</tt> was <tt>EPOLL_CTL_ADD</tt>, and a transport file descriptor is already in <tt>epfd</tt>.<br>
</dd><dt><tt>EINVAL</tt></dt><dd><tt>epfd</tt> is not an <tt>epoll</tt> file descriptor, or the requested operation <tt>op</tt> is not supported by this interface.<br>
</dd><dt><tt>ENOMEM</tt></dt><dd>There was insufficient memory to handle the requested <tt>op</tt> control operation.<br>
</dd></dl>

### Example ###
Wait for one incoming Edge Triggered event on the transport.

```
 int epfd = epoll_create (IP_MAX_MEMBERSHIPS);
 int retval = pgm_transport_epoll_ctl (transport, epfd, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
 struct epoll_event events[1];
 epoll_wait (efd, events, 1, 1000 /* ms */);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportPollInfo.md'>pgm_transport_poll_info()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
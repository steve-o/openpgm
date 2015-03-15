#summary OpenPGM 5 : C Reference : Socket : pgm\_epoll\_ctl()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#define CONFIG_HAVE_EPOLL<br>
#include <pgm/pgm.h><br>
<br>
int *pgm_epoll_ctl* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const    sock,<br>
int                  epfd,<br>
int                  op,<br>
int                  events<br>
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
<td><tt>sock</tt></td>
<td>The PGM socket object.</td>
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
 int retval = pgm_epoll_ctl (sock, epfd, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
 struct epoll_event events[1];
 epoll_wait (efd, events, 1, 1000 /* ms */);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSelectInfo.md'>pgm_select_info()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmPollInfo.md'>pgm_poll_info()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
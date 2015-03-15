#summary OpenPGM : C Reference : pgm\_transport\_recvmsgv()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize] *pgm_transport_recvmsgv* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*      transport,<br>
[OpenPgmCReferencePgmMsgvT pgm_msgv_t]* const     msgv,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                 count,<br>
int                   flags<br>
);<br>
</pre>

### Purpose ###
Receive a vector of Application Protocol Domain Unit's (APDUs) from the transport.

### Remarks ###
The PGM protocol is bi-directional, even send-only applications need to process incoming requests for retransmission (NAKs) and other packet types.  The synchronous API suite provides low level access to the raw events driving the protocol in order to accelerate performance in high message rate environments.

<tt>pgm_transport_recvmsgv()</tt> fills a vector of <tt>pgm_msgv_t</tt> structures with a scatter/gather vector of buffers directly from the receive window.  The vector size is governed by IOV\_MAX, on Linux is 1024, therefore up to 1024 TPDUs or 1024 messages may be returned, whichever is the lowest.  Using the maximum size is not always recommended as time processing the received messages might cause an incoming buffer overrun.

Memory is returned to the receive window on the next call or transport destruction.

Unrecoverable data loss will cause the function to immediately return with <tt>errno</tt> set to <tt>ECONNRESET</tt>.  If <tt><a href='OpenPgmCReferencePgmTransportSetCloseOnFailure.md'>pgm_transport_set_close_on_failure()</a></tt> is called with <tt>FALSE</tt> (the default mode) processing can continue with subsequent calls to <tt>pgm_transport_recvmsgv()</tt>, if <tt>TRUE</tt> then the transport will be closed and subsequent calls will return <tt>ENOTCONN</tt>.

It is valid to send and receive zero length PGM packets.

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
<td><tt>msgv</tt></td>
<td>Message vector of TPDU vectors.</td>
</tr><tr>
<td><tt>count</tt></td>
<td>Elements in <tt>msgv</tt>.</td>
</tr><tr>
<td><tt>flags</tt></td>
<td>Bitwise OR of zero or more flags: <tt>MSG_DONTWAIT</tt> enabling non-blocking operation.</td>
</tr>
</table>


### Return Value ###
On success, returns the number of data bytes read, or -1 if an error occurred and <tt>errno</tt> appropriately set.

### Errors ###
<dl><dt><tt>EAGAIN</tt></dt><dd>The socket is marked non-blocking and the receive operation would block.<br>
</dd><dt><tt>ECONNRESET</tt></dt><dd>Unrecoverable data loss was detected.<br>
</dd><dt><tt>EINTR</tt></dt><dd>The receive was interrupted by delivery of a signal before any data were available.<br>
</dd><dt><tt>ENOMEM</tt></dt><dd>Unable to allocate memory for internal tables.<br>
</dd><dt><tt>ENOTCONN</tt></dt><dd>The transport is closed.<br>
</dd></dl>

### Example ###
Read a maximum size vector from a transport.

```
 gsize iov_max = sysconf( SC_IOV_MAX );
 pgm_msgv_t msgv[iov_max];
 gssize bytes_read = pgm_transport_recvmsgv (transport, msgv, iov_max, MSG_DONTWAIT);
```

Display error details on unrecoverable error.

```
 pgm_msgv_t msgv[10];
 gssize bytes_read = pgm_transport_recvmsgv (transport, msgv, iov_max, MSG_DONTWAIT);
 if (-1 == len && ECONNRESET == errno) {
  pgm_sock_err_t* pgm_sock_err = (pgm_sock_err_t*)msgv[0].msgv_iov->iov_base;
  g_warning ("pgm socket lost %" G_GUINT32_FORMAT " packets detected from %s",
             pgm_sock_err->lost_count,
             pgm_print_tsi(&pgm_sock_err->tsi));
 }
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportRecv.md'>pgm_transport_recv()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
#summary OpenPGM : C Reference : pgm\_transport\_recv()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize] *pgm_recv* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]            buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]               len,<br>
int                 flags<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize] *pgm_recvfrom* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]            buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]               len,<br>
int                 flags,<br>
[OpenPgmCReferencePgmTsiT pgm_tsi_t]*          from<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize] *pgm_recvmsg* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]*    transport,<br>
[OpenPgmCReferencePgmMsgvT pgm_msgv_t]*         msgv,<br>
int                 flags<br>
);<br>
</pre>

### Purpose ###
Receive an Application Protocol Domain Unit (APDU) from the transport.

### Remarks ###
The PGM protocol is bi-directional, even send-only applications need to process incoming requests for retransmission (NAKs) and other packet types.  The synchronous API suite provides low level access to the raw events driving the protocol in order to accelerate performance in high message rate environments.

<tt>pgm_transport_recv()</tt> and <tt>pgm_transport_recvfrom()</tt> fill the provided buffer location with up to <tt>len</tt> contiguous APDU bytes therefore the buffer should be large enough to store the largest APDU expected.

If <tt>from</tt> is not <tt>NULL</tt>, the source TSI is filled in.

The <tt>pgm_transport_recv()</tt> is identical to <tt>pgm_transport_recvfrom()</tt> with a <tt>NULL</tt> <tt>from</tt> parameter.

<tt>pgm_transport_recvmsg()</tt> fills a <tt>pgm_msgv_t</tt> structure with a scatter/gather vector of buffers directly from the receive window.  The vector size is governed by IOV\_MAX, on Linux is 1024.  Using the maximum size is not always recommended as time processing the received messages might cause an incoming buffer overrun.

Memory is returned to the receive window on the next call or transport destruction.

Unrecoverable data loss will cause the function to immediately return with <tt>errno</tt> set to <tt>ECONNRESET</tt> and <tt>buf</tt> populated with a <tt><a href='OpenPgmCReferencePgmSockErrT.md'>pgm_sock_err_t</a></tt> structure detailing the error.  If <tt><a href='OpenPgmCReferencePgmTransportSetCloseOnFailure.md'>pgm_transport_set_close_on_failure()</a></tt> is called with <tt>FALSE</tt> (the default mode) processing can continue with subsequent calls to <tt>pgm_transport_recv()</tt>, if <tt>TRUE</tt> then the transport will be closed and subsequent calls will return <tt>ENOTCONN</tt>.

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
<td><tt>buf</tt></td>
<td>Data buffer to fill.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>buf</tt> in bytes.</td>
</tr><tr>
<td><tt>msgv</tt></td>
<td>Message vector of TPDUs.</td>
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
Receive an APDU up to 4096 bytes in length.

```
 gchar buf[4096];
 gssize bytes_read = pgm_transport_recv (transport, buf, sizeof(buf), 0);
```

Receive an APDU with source TSI.

```
 gchar buf[4096];
 pgm_tsi_t from;
 gssize bytes_read = pgm_transport_recvfrom (transport, buf, sizeof(buf), 0, &from);
 g_message ("%" G_GSSIZE_FORMAT " bytes received from %s", pgm_print_tsi(&from));
```

Receive a scatter/gather vector APDU message.

```
 pgm_msgv_t msgv;
 gssize bytes_read = pgm_transport_recvmsg (transport, &msgv, 0);
 printf ("received %i bytes: ", bytes_read);
 struct iovec* msgv_iov = msgv.msgv_iov;
 while (bytes_read > 0) {
   printf ((char*)msgv_iov->iov_base);
   bytes_read -= msgv_iov->iov_len;
   msgv_iov++;
 }
 putchar('\n');
```

Display error details on unrecoverable error.

```
 gchar buf[4096];
 gssize bytes_read = pgm_transport_recv (transport, buf, sizeof(buf), 0);
 if (-1 == len && ECONNRESET == errno) {
   pgm_sock_err_t* pgm_sock_err = (pgm_sock_err_t*)buf;
   g_warning ("pgm socket lost %" G_GUINT32_FORMAT " packets detected from %s",
              pgm_sock_err->lost_count,
              pgm_print_tsi(&pgm_sock_err->tsi));
 }
```

Display error from <tt>pgm_msgv_t</tt> structure.

```
 pgm_msgv_t msgv;
 gssize bytes_read = pgm_transport_recvmsg (transport, &msgv, 0);
 if (-1 == len && ECONNRESET == errno) {
   pgm_sock_err_t* pgm_sock_err = (pgm_sock_err_t*)msgv.msgv_iov->iov_base;
   g_warning ("pgm socket lost %" G_GUINT32_FORMAT " packets detected from %s",
              pgm_sock_err->lost_count,
              pgm_print_tsi(&pgm_sock_err->tsi));
 }
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmSockErrT.md'>pgm_sock_err_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportRecvMsgv.md'>pgm_transport_recvmsgv()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
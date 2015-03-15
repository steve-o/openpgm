#summary OpenPGM : C Reference : pgm\_transport\_send()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gssize gssize] *pgm_transport_send* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]* const    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gconstpointer gconstpointer]             buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                     count<br>
);<br>
</pre>

### Purpose ###
Send an Application Protocol Data Unit (APDU) to the network.

### Remarks ###
The underlying transport handles packets sized to the maximum transmission protocol data unit (TPDU), when an application provides a larger APDU it will fragment the message into many packets which the receivers will reconstruct and present as a single data unit.

Two methods of non-blocking operation are available, blocking at the underlying transmission data unit, or blocking at the application data unit.  Specify <tt>MSG_DONTWAIT</tt> to block at the underlying transmission data unit, or specify <tt>MSG_DONTWAIT | MSG_WAITALL</tt> to block at the application data unit.

A would be blocked operation might have sent one or more transmission data units.  The context is saved internally but subsequent call should be either <tt>pgm_transport_send()</tt> with the exact same parameters, or <tt><a href='OpenPgmCReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt>.  Put another way: in a multi-threaded application a locking mechanism should be implemented to prevent another thread trying to send a different application data unit.

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
<td>Data buffer to send.</td>
</tr><tr>
<td><tt>count</tt></td>
<td>Length of <tt>buf</tt> in bytes.</td>
</tr><tr>
<td><tt>flags</tt></td>
<td>Bitwise OR of zero or more of the following flags:<br>
<dl><dt><tt>MSG_DONTWAIT</tt></dt><dd>Enables non-blocking operation; if sending a packet would block, <tt>EAGAIN</tt> is returned.<br>
</dd><dt><tt>MSG_WAITALL</tt></dt><dd>Requests that the operation block until the full send can be satisified.</dd></dl></td>
</tr>
</table>


### Return Value ###
On success, returns the number of data bytes sent (not transport bytes).  On error, -1 is returned and <tt>errno</tt> set appropriately.

### Errors ###
<dl><dt><tt>EAGAIN</tt></dt><dd>The socket is marked non-blocking and the requested operation would block.<br>
</dd><dt><tt>EMSGSIZE</tt></dt><dd>The size of the message to be sent is too large.<br>
</dd><dt><tt>ENOBUFS</tt></dt><dd>The  output queue for a network interface was full.  This generally indicates that the interface has stopped sending, but may be caused by transient congestion.  (Normally, this does not occur in Linux. Packets are just silently dropped when a device queue overflows.)<br>
</dd><dt><tt>ENOMEM</tt></dt><dd>No memory available.<br>
</dd><dt><tt>ECONNRESET</tt></dt><dd>Transport was reset due to unrecoverable data loss with close-on-failure enabled with <tt><a href='OpenPgmCReferencePgmTransportSetCloseOnFailure.md'>pgm_transport_set_close_on_failure()</a>.</tt>.<br>
</dd></dl>

### Example ###
Send the traditional Hello world.

```
 gchar* s = "hello world!";
 pgm_transport_send (transport, s, strlen(s) + 1, 0);
```

Basic packet level non-blocking send.

```
 gchar* s = "hello world!";
 size_t len = strlen (s) + 1;
 gssize sent;
 do {
   sent = pgm_transport_send (transport, s, len, MSG_DONTWAIT);
 } while (sent == -1 && errno == EAGAIN);
```

Application level non-blocking send.

```
 gchar* s = "hello world!";
 size_t len = strlen (s) + 1;
 gssize sent;
 do {
   sent = pgm_transport_send (transport, s, len, MSG_DONTWAIT | MSG_WAITALL);
 } while (sent == -1 && errno == EAGAIN);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportSendv.md'>pgm_transport_sendv()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
#summary OpenPGM 2 : C Reference : Asynchronous Receiver : pgm\_async\_recv()
#labels Phase-Implementation
#sidebar TOC2CReferenceAsynchronousReceiver
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-IO-Channels.html#GIOStatus GIOStatus] *pgm_async_recv* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]* const    async,<br>
const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]        buf,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                 len,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]* const          bytes_read,<br>
int                   flags,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**              error<br>
);<br>
</pre>

### Purpose ###
Receive an Application Protocol Domain Unit (APDU) from an asynchronous queue.

### Remarks ###
<tt>pgm_async_recv()</tt> fills the provided buffer location with up to <tt>len</tt> contiguous APDU bytes  therefore the buffer should be large enough to store the largest APDU expected.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>async</tt></td>
<td>The asynchronous receiver object.</td>
</tr><tr>
<td><tt>buf</tt></td>
<td>Data buffer to fill.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>buf</tt> in bytes.</td>
</tr><tr>
<td><tt>bytes_read</tt></td>
<td>Number of bytes read into <tt>buf</tt>.</td>
</tr><tr>
<td><tt>flags</tt></td>
<td>Bitwise OR of zero or more flags: <tt>MSG_DONTWAIT</tt> enabling non-blocking operation on Unix platforms.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>

### Return Value ###
On success, [G\_IO\_STATUS\_NORMAL](http://library.gnome.org/devel/glib/stable/glib-IO-Channels.html#GIOStatus) is returned.  On error, [G\_IO\_STATUS\_ERROR](http://library.gnome.org/devel/glib/stable/glib-IO-Channels.html#GIOStatus) is returned, and if set, <tt>error</tt> may be set appropriately.  If receiver is marked non-blocking and the receive operation would block, [G\_IO\_STATUS\_AGAIN](http://library.gnome.org/devel/glib/stable/glib-IO-Channels.html#GIOStatus) is returned.

### Example ###
Receive an APDU up to 4096 bytes in length.

```
 gchar buf[4096];
 gsize bytes_read 
 if (G_IO_STATUS_NORMAL == pgm_async_recv (async, buf, sizeof(buf), &bytes_read, 0, NULL)) {
   g_message ("Received %d bytes: \"%s\"", bytes_read, buf);
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmRecv.md'>pgm_recv()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.<br>
</li><li><a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html'>GLib Error Reporting</a>.
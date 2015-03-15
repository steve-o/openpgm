#summary OpenPGM 2 : C Reference : Asynchronous Receiver : pgm\_async\_create()
#labels Phase-Implementation
#sidebar TOC2CReferenceAsynchronousReceiver
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_async_create* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]**             async,<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t] const*    transport,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**                  error<br>
);<br>
</pre>

### Purpose ###
Create an asynchronous event handler.

### Remarks ###
APDUs are stored in an asynchronous queue using event objects, memory is managed via the GLib [slab allocator](http://library.gnome.org/devel/glib/stable/glib-Memory-Slices.html).

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
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.  If <tt>err</tt> is provided it may be set detailing the fault.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.<br>
</li><li><a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html'>GLib Error Reporting</a>.
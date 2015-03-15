#summary OpenPGM 2 : C Reference : pgm\_async\_destroy()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_async_destroy* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]**    async<br>
);<br>
</pre>

### Purpose ###
Destroy an asynchronous event handler.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>async</tt></td>
<td>The asynchronous receiver object.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.
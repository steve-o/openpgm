#summary OpenPGM 2 : C Reference : Asynchronous Receiver : pgm\_async\_create\_watch()
#labels Phase-Implementation
#sidebar TOC2CReferenceAsynchronousReceiver
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GSource GSource]* *pgm_async_create_watch* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]*    async<br>
);<br>
</pre>

### Purpose ###
Create a transport event listener.

### Remarks ###
Creates a [GSource](http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GSource) that is dispatched when an incoming message is ready to be processed.  The [GSource](http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GSource) should be added to the !GLib main loop with <tt><a href='http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#g-source-attach'>g_source_attach()</a></tt>.  <tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch()</a></tt> can be used to create and attach the listener to the current main loop with one call.

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
On success, the newly-created [GSource](http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GSource).  If <tt>async</tt> is invalid, <tt>NULL</tt> is returned.

### Example ###
```
 pgm_async_t* async;
 pgm_async_create (&async, transport, 0);
 GSource* source = pgm_async_create_watch (async);
 g_source_set_priority (source, G_PRIORITY_HIGH);
 g_source_attach (source, NULL);
 g_source_unref (source);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch_full()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.
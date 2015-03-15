#summary OpenPGM : C Reference : pgm\_async\_add\_watch()
#labels Phase-Implementation
#sidebar TOCCReference

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_async_add_watch* (<br>
[OpenPgmCReferencePgmAsyncT pgm_async_t]*       async,<br>
[OpenPgmCReferencePgmEventFnT pgm_eventfn_t]      function,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]           user_data<br>
);<br>
<br>
int *pgm_async_add_watch_full* (<br>
[OpenPgmCReferencePgmAsyncT pgm_async_t]*       async,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gint gint]               priority,<br>
[OpenPgmCReferencePgmEventFnT pgm_eventfn_t]      function,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]           user_data,<br>
[http://library.gnome.org/devel/glib/stable/glib-Datasets.html#GDestroyNotify GDestroyNotify]     notify<br>
);<br>
</pre>

### Purpose ###
Add a transport event listener.

### Remarks ###
Event management is handled by the GLib general-purpose utility library, <tt>pgm_transport_add_watch()</tt> creates an event listener in the current context for the specified transport.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>async</tt></td>
<td>The PGM asynchronous receiver object.</td>
</tr><tr>
<td><tt>priority</tt></td>
<td>The priority of the listen source.  Typically this will be in the range between <a href='http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#G-PRIORITY-DEFAULT:CAPS'>G_PRIORITY_DEFAULT</a> and <a href='http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#G-PRIORITY-HIGH:CAPS'>G_PRIORITY_HIGH</a>.</td>
</tr><tr>
<td><tt>function</tt></td>
<td>Function to call</td>
</tr><tr>
<td><tt>user_data</tt></td>
<td>Data to pass to <tt>function</tt></td>
</tr><tr>
<td><tt>notify</tt></td>
<td>Function to call when the listener is destroyed.</td>
</tr>
</table>


### Return Value ###
On success, the ID (greater than 0) of the event source.  On invalid arguments, <tt>-EINVAL</tt> is returned.

### Example ###
```
 int
 on_data (
     gpointer data,
     guint len,
     gpointer user_data)
 {
   char buf[1024];
   snprintf (buf, sizeof(buf), "%s", (char*)data);
   g_message ("\"%s\" (%i bytes)", buf, len);
   return 0;
 }
 
 pgm_async_t* async;
 pgm_async_create (&async, transport, 0);
 pgm_async_add_watch (async, on_data, NULL);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmEventFnT.md'>pgm_eventfn_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
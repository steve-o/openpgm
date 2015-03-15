#summary OpenPGM 2 : C Reference : Asynchronous Receiver : pgm\_async\_set\_nonblocking()
#labels Phase-Implementation
#sidebar TOC2CReferenceAsynchronousReceiver
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_async_set_nonblocking* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]* const    async,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]              nonblocking<br>
);<br>
</pre>

### Purpose ###
Set asynchronous operation to blocking or non-blocking mode.

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
<td><tt>nonblocking</tt></td>
<td>Enable asynchronous receiver non-blocking.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.

### Example ###

```
 pgm_async_set_nonblocking (async, TRUE);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.
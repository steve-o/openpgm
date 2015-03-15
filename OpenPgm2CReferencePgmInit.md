_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_init* ([http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**     error);<br>
</pre>

### Purpose ###
Create and start OpenPGM internal machinery.

### Remarks ###
This call creates the internal machinery that OpenPGM requires for its operation:

  * Threading support.
  * Timer support.
  * PGM protocol number.

Until the first call to <tt>pgm_init()</tt>, all events, queues, and transports are unusable.

<tt>pgm_init()</tt> might only be called once. On the second call it will abort with an error. If you want to make sure that the PGM engine is initialized, you can do this:

```
 if (!pgm_supported ()) pgm_init (NULL);
```

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>err</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>


### Return Value ###
On success, returns [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS).  On failure, returns [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS).  If <tt>err</tt> is provided it may be set detailing any Winsock or timing device initialization issue.


### Example ###
```
 GError *err = NULL;
 if (!pgm_init (&err)) {
   g_warning ("PGM Engine initialization error %s", err->message);
   g_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmSupported.md'>pgm_supported()</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmShutdown.md'>pgm_shutdown()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.
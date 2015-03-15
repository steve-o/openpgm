#summary OpenPGM 2 : C Reference : Monitoring and Administration : pgm\_http\_init()
#labels Phase-Implementation
#sidebar TOC2CReferenceMonitoringAndAdministration
_Function_
### Declaration ###
<pre>
#include <pgm/http.h><br>
<br>
#define PGM_HTTP_DEFAULT_SERVER_PORT    4968<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_http_init* (<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]        server_port,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**     error<br>
);<br>
</pre>

### Purpose ###
Create and start a HTTP administration interface.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>server_port</tt></td>
<td>HTTP port to listen on.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.  If <tt>err</tt> is provided it may be set detailing the fault.

### Example ###

```
 GError* err = NULL;
 if (!pgm_http_init (PGM_HTTP_DEFAULT_SERVER_PORT, &err)) {
   g_error ("Unable to start HTTP interface: %s", err->message);
   g_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmHttpShutdown.md'>pgm_http_shutdown()</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmSnmpInit.md'>pgm_snmp_init()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceMonitoringAndAdministration.md'>Monitoring and Administration</a> in OpenPGM C Reference.<br>
</li><li><a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html'>GLib Error Reporting</a>.
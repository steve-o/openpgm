#summary OpenPGM 2 : C Reference : Transport : pgm\_gsi\_create\_from\_addr()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_gsi_create_from_addr* (<br>
[OpenPgm2CReferencePgmGsiT pgm_gsi_t]*    gsi,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**     error<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on the nodes IPv4 address.

### Remarks ###
A globally unique source identifier (GSI) is required for PGM to track the sending state of peers.  This function generates a GSI compatible with TIBCO Rendezvous which combines the four octets of the host IP address with a random number.  The random number is to help indicate if a process has been restarted as the transmit window is no longer valid.

The GSI is specified when creating a new PGM transport with <tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt>.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError'>GError</a>, or <a href='http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#NULL--CAPS'>NULL</a>.</td>
</tr>
</table>

### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On error, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned.  If <tt>error</tt> is provided it may be set detailing the fault.

### Example ###
Basic usage without error handling.

```
 pgm_gsi_t gsi;
 pgm_gsi_create_from_addr (&gsi, NULL);
```

Display an error message detailing failure.

```
 pgm_gsi_t gsi;
 GError *err = NULL;
 if (!pgm_gsi_create_from_addr (&gsi, &err)) {
   g_message ("GSI failed: %s", err->message);
   g_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.<br>
</li><li><a href='http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html'>GLib Error Reporting</a>.
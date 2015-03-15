_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean] *pgm_if_get_transport_info* (<br>
const char*                            network,<br>
struct [OpenPgm2CReferencePgmTransportInfoT pgm_transport_info_t] const*     hints,<br>
struct [OpenPgm2CReferencePgmTransportInfoT pgm_transport_info_t]**          res,<br>
[http://library.gnome.org/devel/glib/stable/glib-Error-Reporting.html#GError GError]**                               error<br>
);<br>
<br>
void *pgm_if_free_transport_info* (<br>
struct [OpenPgm2CReferencePgmTransportInfoT pgm_transport_info_t]*           res<br>
);<br>
</pre>

### Purpose ###
Decompose a string network specification.

### Remarks ###
The TIBCO Rendezvous network parameter provides a convenient compact representation of the complicated data structures needed to specify sending and receiving interfaces and multicast addresses.  This function provides conversion between the network string and the parameters of <tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt>.

The returned <tt><a href='OpenPgm2CReferencePgmTransportInfoT.md'>pgm_transport_info_t</a></tt> must be freed with <tt>pgm_if_free_transport_info()</tt>.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>network</tt></td>
<td>A TIBCO Rendezvous compatible network parameter string</td>
</tr><tr>
<td><tt>hints</tt></td>
<td>Transport info structure whose <tt>ti_family</tt> specify criteria that limit the set of addresses returned by <tt>pgm_if_get_transport_info()</tt>.</td>
</tr><tr>
<td><tt>res</tt></td>
<td>return location for resolved transport info structure.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>return location for error.</td>
</tr>
</table>


### Return Value ###
On success, [TRUE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#TRUE--CAPS) is returned.  On failure, [FALSE](http://library.gnome.org/devel/glib/stable/glib-Standard-Macros.html#FALSE--CAPS) is returned and if <tt>error</tt> is set it may be populated with details of the exception.

### Example ###
Basic example using default hints.
```
 char* network = "eth0;226.0.0.1";
 struct pgm_transport_info_t* res = NULL;
 
 if (!pgm_if_get_transport_info (network, NULL, &res, &err)) {
   g_error ("parsing network parameter: %s", err->message);
   g_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportInfoT.md'>pgm_transport_info_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceInterface.md'>Interface</a> in OpenPGM C Reference.</li></ul>

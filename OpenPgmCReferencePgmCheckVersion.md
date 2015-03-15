_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
extern const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint] *pgm_major_version*;<br>
extern const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint] *pgm_minor_version*;<br>
extern const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint] *pgm_micro_version*;<br>
</pre>

<strike><pre>
const [http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_check_version*' (<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]      required_major,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]      required_minor,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]      required_micro<br>
);</pre></strike>

### Purpose ###
OpenPGM version information.

### Parameters ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>required_major</tt></td>
<td>The required major version.</td>
</tr><tr>
<td><tt>required_minor</tt></td>
<td>The required minor version.</td>
</tr><tr>
<td><tt>required_micro</tt></td>
<td>The required micro version.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>NULL</tt>, on error returns a string describing the mismatch.

### See Also ###
  * [Environment](OpenPgmCReferenceEnvironment.md) in OpenPGM C Reference.
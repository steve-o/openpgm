#summary OpenPGM 2 : C Reference : Transport : pgm\_tsi\_print()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
#define PGM_TSISTRLEN (sizeof("000.000.000.000.000.000.00000"))<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gchar gchar]* *pgm_tsi_print* (<br>
const [OpenPgm2CReferencePgmTsiT pgm_tsi_t]*    tsi<br>
);<br>
<br>
int* *pgm_tsi_print_r* (<br>
const [OpenPgm2CReferencePgmTsiT pgm_tsi_t]*    tsi,<br>
char*               buf,<br>
size_t              bufsize<br>
);<br>
</pre>

### Purpose ###
Display a TSI in human friendly form.

### Remarks ###
The globally unique transport session identifier (TSI) is composed of the concatenation of a globally unique source identifier (GSI) and a source-assigned data-source port.

<tt>pgm_tsi_print_r</tt> returns the same identifier in the array <tt>buf</tt> of size <tt>bufsize</tt>.

<tt>bufsize</tt> should be at least <tt>PGM_TSISTRLEN</tt> in length.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>tsi</tt></td>
<td>Globally unique transport session identifier (TSI).</td>
</tr><tr>
<td><tt>buf</tt></td>
<td>Buffer to store GSI.</td>
</tr><tr>
<td><tt>bufsize</tt></td>
<td>Length of buffer <tt>buf</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>pgm_tsi_print</tt> returns a pointer to the TSI, and <tt>NULL</tt> on failure.  <tt>pgm_tsi_print_r</tt> returns 0 when successful, and <tt>-EINVAL</tt> on failure.

### Example ###
Display TSI in non-re-entrant form.

```
 printf ("TSI: %s\n", pgm_tsi_print (&transport->tsi));
```

Display TSI in re-entrant form.

```
 char buf[PGM_TSISTRLEN];
 pgm_tsi_print_r (&transport->tsi, buf, sizeof(buf));
 printf ("TSI: %s\n", buf);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
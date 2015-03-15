#summary OpenPGM : C Reference : pgm\_tsi\_t
#labels Phase-Implementation

_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_tsi_t *pgm_tsi_t*;<br>
<br>
struct pgm_tsi_t {<br>
[OpenPgmCReferencePgmGsiT pgm_gsi_t]     gsi;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]       sport;<br>
};<br>
</pre>

### Purpose ###
A transport session identifier (TSI).

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmPrintTsi.md'>pgm_print_tsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmPrintTsi.md'>pgm_print_tsi_r()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.</li></ul>

#summary OpenPGM 2 : C Reference : Transport : pgm\_tsi\_t
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
/* maximum length of TSI as a string */<br>
#define PGM_TSISTRLEN   (sizeof("000.000.000.000.000.000.00000"))<br>
<br>
typedef struct pgm_tsi_t *pgm_tsi_t*;<br>
<br>
struct pgm_tsi_t {<br>
[OpenPgm2CReferencePgmGsiT pgm_gsi_t]     gsi;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]       sport;<br>
};<br>
</pre>

### Purpose ###
A transport session identifier (TSI).

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTsiPrint.md'>pgm_tsi_print()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTsiPrint.md'>pgm_tsi_print_r()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
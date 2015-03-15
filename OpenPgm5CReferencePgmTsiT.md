#summary OpenPGM 5 : C Reference : Socket : pgm\_tsi\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
/* maximum length of TSI as a string */<br>
#define PGM_TSISTRLEN   (sizeof("000.000.000.000.000.000.00000"))<br>
#define PGM_TSI_INIT    { PGM_GSI_INIT, 0 }<br>
<br>
typedef struct pgm_tsi_t *pgm_tsi_t*;<br>
<br>
struct pgm_tsi_t {<br>
[OpenPgm5CReferencePgmGsiT pgm_gsi_t]     gsi;<br>
uint16_t      sport;<br>
};<br>
</pre>

### Purpose ###
A transport session identifier (TSI).

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmTsiPrint.md'>pgm_tsi_print()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmTsiPrint.md'>pgm_tsi_print_r()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
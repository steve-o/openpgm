#summary OpenPGM 5 : C Reference : Socket : pgm\_gsi\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
/* maximum length of GSI as a string */<br>
#define PGM_GSISTRLEN   (sizeof("000.000.000.000.000.000"))<br>
#define PGM_GSI_INIT    {{ 0, 0, 0, 0, 0, 0 }}<br>
<br>
typedef struct pgm_gsi_t *pgm_gsi_t*;<br>
<br>
struct pgm_gsi_t {<br>
char identifier[6];<br>
};<br>
</pre>

### Purpose ###
A globally unique source identifier (GSI).

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
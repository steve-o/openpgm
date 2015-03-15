#summary OpenPGM 2 : C Reference : Transport : pgm\_gsi\_t
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
/* maximum length of GSI as a string */<br>
#define PGM_GSISTRLEN   (sizeof("000.000.000.000.000.000"))<br>
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
  * <tt><a href='OpenPgm2CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
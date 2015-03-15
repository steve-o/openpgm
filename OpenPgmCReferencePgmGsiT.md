#summary OpenPGM : C Reference : pgm\_gsi\_t
#labels Phase-Implementation

_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
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
  * <tt><a href='OpenPgmCReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmCreateIPv4Gsi.md'>pgm_create_ipv4_gsi()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmCreateMD5Gsi.md'>pgm_create_md5_gsi()</a></tt>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
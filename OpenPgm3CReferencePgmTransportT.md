_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_transport_t *pgm_transport_t*;<br>
</pre>

### Purpose ###
A transport object represents a delivery mechanism for messages.

### Remarks ###
A transport describes a carrier mechanism for messages across a network.

Programs must explicitly destroy each transport object. Destroying a transport object invalidates subsequent send calls on that transport, invalidates any listeners using that transport, and frees its storage.

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_sock_t *pgm_sock_t*;<br>
</pre>

### Purpose ###
A PGM socket object represents a delivery mechanism for messages.

### Remarks ###
A PGM socket describes a carrier mechanism for messages across a network.

Programs must explicitly destroy each transport object. Destroying a PGM socket object invalidates subsequent send calls on that transport, invalidates any listeners using that transport, and frees its storage.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
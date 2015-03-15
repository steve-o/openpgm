#summary OpenPGM : C Reference : pgm\_sock\_err\_t
#labels Phase-Implementation

_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_sock_err_t *pgm_sock_err_t*;<br>
<br>
struct pgm_sock_err_t {<br>
[OpenPgmCReferencePgmTsiT pgm_tsi_t]      tsi;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint32 guint32]        lost_count;<br>
};<br>
</pre>

### Purpose ###
A structure detailing unrecoverable data loss.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
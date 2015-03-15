#summary OpenPGM 5 : C Reference : Socket : pgm\_sockaddr\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_sockaddr_t {<br>
uint16_t      sa_port;    /* data-destination port */<br>
[OpenPgm5CReferencePgmTsiT pgm_tsi_t]     sa_addr;<br>
};<br>
</pre>

### Purpose ###
PGM endpoint address.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
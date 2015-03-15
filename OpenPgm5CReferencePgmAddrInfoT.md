#summary OpenPGM 5 : C Reference : Interface : struct pgm\_addrinfo\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceInterface
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_addrinfo_t {<br>
sa_family_t                           ai_family;<br>
uint32_t                              ai_recv_addrs_len;<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]* restrict     ai_recv_addrs;<br>
uint32_t                              ai_send_addrs_len;<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]* restrict     ai_send_addrs;<br>
};<br>
</pre>

### Purpose ###
A transport object represents network definition of a PGM transport.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmGetAddrInfo.md'>pgm_getaddrinfo()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmGetAddrInfo.md'>pgm_freeaddrinfo()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceInterface.md'>Interface</a> in OpenPGM C Reference.
#summary OpenPGM : C Reference : pgm\_msgv\_t
#labels Phase-Implementation

_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_msgv_t pgm_msgv_t*;<br>
<br>
struct pgm_msgv_t {<br>
[OpenPgmCReferencePgmTsiT pgm_tsi_t]*      msgv_tsi;       /* TSI */<br>
struct iovec*   msgv_iov;       /* scatter/gather array */<br>
size_t          msgv_iovlen;    /* # elements in iov */<br>
};<br>
</pre>

### Purpose ###
A scatter/gather message vector.

### Remarks ###
Received messages from a transport can be a populated scatter/gather vector.  APDU arrays are owned by the transport object, and the TPDU buffers are owned by the receive window and both will be recycled on the next call.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportRecv.md'>pgm_transport_recvmsg()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportRecvMsgv.md'>pgm_transport_recvmsgv()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
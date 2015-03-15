#summary OpenPGM 2 : C Reference : Transport : pgm\_msgv\_t
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
#define PGM_MAX_FRAGMENTS 16<br>
<br>
typedef struct pgm_msgv_t pgm_msgv_t;<br>
<br>
struct pgm_msgv_t {<br>
size_t                msgv_len;                        /* number of elements in msgv_skb */<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]* msgv_skb[PGM_MAX_FRAGMENTS];     /* PGM socket buffer array */<br>
};<br>
</pre>

### Purpose ###
A scatter/gather message vector<sup>2</sup>.

### Remarks ###
Received messages from a transport can be a populated scatter/gather vector. APDU arrays are owned by the transport object, and the TPDU buffers are owned by the receive window and both will be recycled on the next call.

### See Also ###
  * <tt>struct <a href='OpenPgm2CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmRecv.md'>pgm_recvmsg()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmRecvMsgv.md'>pgm_recvmsgv()</a></tt><br>
</li><li><a href='OpenPgm2CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
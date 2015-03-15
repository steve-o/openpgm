#summary OpenPGM 5 : C Reference : Socket : pgm\_msgv\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
#define PGM_MAX_FRAGMENTS 16<br>
<br>
typedef struct pgm_msgv_t *pgm_msgv_t*;<br>
<br>
struct pgm_msgv_t {<br>
size_t                msgv_len;                        /* number of elements in msgv_skb */<br>
struct [OpenPgm5CReferencePgmSkBuffT pgm_sk_buff_t]* msgv_skb[PGM_MAX_FRAGMENTS];     /* PGM socket buffer array */<br>
};<br>
</pre>

### Purpose ###
A scatter/gather message vector<sup>2</sup>.

### Remarks ###
Received messages from a transport can be a populated scatter/gather vector. APDU arrays are owned by the transport object, and the TPDU buffers are owned by the receive window and both will be recycled on the next call.

### See Also ###
  * <tt>struct <a href='OpenPgm5CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmRecv.md'>pgm_recvmsg()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmRecvMsgv.md'>pgm_recvmsgv()</a></tt><br>
</li><li><a href='OpenPgm5CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
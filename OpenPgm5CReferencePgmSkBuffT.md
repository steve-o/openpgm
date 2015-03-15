#summary OpenPGM 5 : C Reference : PGM SKBs : struct pgm\_sk\_buff\_t
#labels Phase-Implementation
#sidebar TOC5CReferencePgmSkbs
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_sk_buff_t {<br>
pgm_list_t                   link_;<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]*                  sock;<br>
[OpenPgm5CReferencePgmTimeT pgm_time_t]                   tstamp;<br>
[OpenPgm5CReferencePgmTsiT pgm_tsi_t]                    tsi;<br>
uint32_t                     sequence;<br>
uint32_t                     __padding;<br>
char                         cb[48];<br>
uint16_t                     len;<br>
unsigned                     zero_padded:1;<br>
struct pgm_header*           pgm_header;<br>
struct pgm_opt_fragment*     pgm_opt_fragment;<br>
#define of_apdu_first_sqn            pgm_opt_fragment->opt_sqn<br>
#define of_frag_offset               pgm_opt_fragment->opt_frag_off<br>
#define of_apdu_len                  pgm_opt_fragment->opt_frag_len<br>
struct pgm_opt_pgmcc_data*   pgm_opt_pgmcc_data;<br>
struct pgm_data*             pgm_data;<br>
void*                        head;           /* all may-alias */<br>
void*                        data;<br>
void*                        tail;<br>
void*                        end;<br>
uint32_t                     truesize;<br>
uint32_t                     users;          /* atomic */<br>
};<br>
</pre>

### Purpose ###
Object representing a PGM socket buffer.


### Remarks ###
Incoming SKBs are tagged after the call to <tt>recvmsg</tt> or <tt>WSARecvMsg</tt> completes, incurring an inaccuracy due to propagation delays and scheduling.

Whilst [kernel timestamping](http://lwn.net/Articles/325929/) mechanisms exist they are system specific.  Not all NICs support hardware timestamping, there are no guarantees that the NIC clock is monotonic, or even functions correctly at all and hence the extended API for software stamping fallback if the NIC failed.  The NIC clock need not be related to real time or core startup time, for instance it could be an offset to when the NIC processor started.  As the clock offset and resolution is likely to be incompatible to what is used for state timekeeping within OpenPGM this feature is not currently used.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmTimeT.md'>pgm_time_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmAllocSkb.md'>pgm_free_skb()</a></tt><br>
</li><li><a href='OpenPgm5CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.
#summary OpenPGM 5 : C Reference : Socket : pgm\_pgmccinfo\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_pgmccinfo_t {<br>
uint32_t       ack_bo_ivl;<br>
uint32_t       ack_c;           /* constant C */<br>
uint32_t       ack_c_p;         /* constant Cp */<br>
};<br>
</pre>

### Purpose ###
Settings for PGM Congestion Control (PGMCC)

### Remarks ###
The following parameters of PGMCC are fixed values:

  * <tt>ssthresh</tt>, slow-start threshold to slowly probe the network to determine its available capacity.  Setting is currently 4 packets as per PGMCC draft recommendations.
  * Packet loss is assumed after _3 duplicate ACKs_.


### See Also ###
  * [Socket](OpenPgm5CReferenceSocket.md) in OpenPGM C Reference.<br>
<ul><li><a href='http://en.wikipedia.org/wiki/Slow-start'>Slow-start</a> in Wikipedia.
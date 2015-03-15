#summary OpenPGM 5 : C Reference : Socket : pgm\_fecinfo\_t
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_fecinfo_t {<br>
uint8_t        block_size;                /* RS(n) */<br>
uint8_t        proactive_packets;<br>
uint8_t        group_size;                /* RS(k) */<br>
bool           ondemand_parity_enabled;<br>
bool           var_pktlen_enabled;<br>
};<br>
</pre>

### Purpose ###
Settings for forward error correction (FEC).

### See Also ###
  * [Socket](OpenPgm5CReferenceSocket.md) in OpenPGM C Reference.<br>
<ul><li><a href='http://msdn.microsoft.com/en-us/library/ms740132(VS.85).aspx'>RM_FEC_INFO</a> structure on MSDN.
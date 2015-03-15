_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
enum /* PGMIOStatus */ {<br>
PGM_IO_STATUS_ERROR,           /* an error occurred */<br>
PGM_IO_STATUS_NORMAL,          /* success */<br>
PGM_IO_STATUS_RESET,           /* session reset */<br>
PGM_IO_STATUS_FIN,             /* session finished */<br>
PGM_IO_STATUS_EOF,             /* transport closed */<br>
PGM_IO_STATUS_WOULD_BLOCK,     /* resource temporarily unavailable */<br>
PGM_IO_STATUS_RATE_LIMITED,    /* would-block on rate limit, check timer */<br>
PGM_IO_STATUS_TIMER_PENDING,   /* would-block with pending timer */<br>
};<br>
</pre>

### Purpose ###
Return status for PGM IO functions.

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmRecv.md'>pgm_recv()</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmSend.md'>pgm_send()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
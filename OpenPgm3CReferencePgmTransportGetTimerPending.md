#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_get\_timer\_pending()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_get_timer_pending* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport,<br>
struct timeval*           tv<br>
);<br>
</pre>

### Purpose ###
Get time before next timer event.

### Remarks ###
A transport set as non-blocking with <tt><a href='OpenPgm3CReferencePgmTransportSetNonBlocking.md'>pgm_transport_set_nonblocking()</a></tt> will return <tt>PGM_IO_STATUS_TIMER_PENDING</tt> on calls to <tt><a href='OpenPgm3CReferencePgmRecv.md'>pgm_recv()</a></tt> that have a pending timing event.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>tv</tt></td>
<td>Pointer to store time remaining.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.

### Example ###

```
  char s[1024];
  int status;
 again:
   status = pgm_recv (transport, s, sizeof(s), 0, NULL, NULL);
   if (PGM_IO_STATUS_TIMER_PENDING == status) {
     struct timeval tv;
     pgm_transport_get_timer_pending (transport, &tv);
     usleep ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
     goto again;
   }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmIoStatus.md'>PGMIOStatus</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmRecv.md'>pgm_recv()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
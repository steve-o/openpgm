#summary OpenPGM 3 : C Reference : Transport : pgm\_send\_skbv()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_send_skbv* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const           transport,<br>
const struct [OpenPgm3CReferencePgmSkBuffT pgm_sk_buff_t]**     vector,<br>
unsigned                            count,<br>
bool                         is_one_apdu,<br>
size_t*                           bytes_written<br>
);<br>
</pre>

### Purpose ###
Send a vector of transmit window buffers as one Application Protocol Data Unit (APDU), or a vector of APDUs to the network.

### Remarks ###
Network delivery of APDUs can be interspersed with ambient SPM and RDATA packets as necessary.  Vector element PGM SKBs must be retrieved from the transmit window via <tt><a href='OpenPgm3CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a>.</tt>

Calling with non-blocking transport set will return with <tt>PGM_IO_STATUS_WOULD_BLOCK</tt> or <tt>PGM_IO_STATUS_RATE_LIMITED</tt>. on any TPDU in the APDU failing to be immediately delivered.  The next call to <tt>pgm_sendv()</tt> MUST be with exactly the same parameters until transmission is complete.

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
<td><tt>vector</tt></td>
<td>Scatter/gather IO vector of data buffers.</td>
</tr><tr>
<td><tt>count</tt></td>
<td>Elements in <tt>vector</tt>.</td>
</tr><tr>
<td><tt>is_one_apdu</tt></td>
<td><tt>vector</tt> is one APDU if set <tt>true</tt>, every element of <tt>vector</tt> is one APDU if set to <tt>false</tt>.</td>
</tr><tr>
<td><tt>bytes_written</tt></td>
<td>Pointer to store count of bytes written from <tt>buf</tt>.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>PGM_IO_STATUS_NORMAL</tt>, on error returns <tt>PGM_IO_STATUS_ERROR</tt>, on reset due to unrecoverable data loss, returns <tt>PGM_IO_STATUS_RESET</tt>.  If the transport is marked non-blocking then <tt>PGM_IO_STATUS_WOULD_BLOCK</tt> is returned if the operation would block, if the block would be caused by the rate limiting engine then <tt>PGM_IO_STATUS_RATE_LIMITED</tt> is returned instead.

### Example ###
Send with non-blocking APDU with basic event detection.

```
 const char* buffer = "안녕하세요, 잘 지내 시죠?";
 struct pgm_sk_buff_t* vector[2];
 const size_t max_tpdu = 1500;
 const size_t header_size = pgm_transport_pkt_offset (true);
 vector[0] = pgm_alloc_skb (max_tpdu);
 pgm_skb_reserve (vector[0], header_size);
 pgm_skb_put (vector[0], wcslen (buffer) / 2);
 memcpy (vector[0]->data, buffer, vector[0]->len);
 vector[1] = pgm_alloc_skb (max_tpdu);
 pgm_skb_reserve (vector[1], header_size);
 pgm_skb_put (vector[1], wcslen(buffer) - (wcslen (buffer) / 2);
 memcpy (vector[1]->data, buffer + vector[0]->len, vector[1]->len);
 for(;;) {
   size_t bytes_sent;
   int status;
   status = pgm_send_skbv (transport, vector, 2, true, &bytes_sent);
   if (PGM_IO_STATUS_WOULD_BLOCK == status) {
     int n_fds = 3;
     struct pollfd fds[ n_fds ];
     memset (fds, 0, sizeof(fds));
     pgm_transport_poll_info (transport, fds, &n_fds, POLLIN);
     poll (fds, n_fds, -1 /* timeout=∞ */);
   } else if (PGM_IO_STATUS_RATE_LIMITED == status) {
     struct timeval tv;
     useconds_t timeout;
     pgm_transport_get_rate_remaining (transport, &tv);
     timeout = (tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000);
     usleep (timeout);
   } else {
     break;
   }
   printf ("sent %zu bytes.\n", bytes_sent);
 }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmIoStatus.md'>PGMIOStatus</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmSend.md'>pgm_send()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmSendv.md'>pgm_sendv()</a></tt><br>
</li><li><a href='OpenPgm3CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
#summary OpenPGM 2 : C Reference : Transport : pgm\_sendv()
#labels Phase-Implementation
#sidebar TOC2CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[OpenPgm2CReferencePgmIoStatus PGMIOStatus] *pgm_sendv* (<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]* const      transport,<br>
const struct [OpenPgm2CReferencePgmIoVec pgm_iovec]*     vector,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                       count,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gboolean gboolean]                    is_one_apdu,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]*                      bytes_written<br>
);<br>
</pre>

### Purpose ###
Send a vector of application buffers as one Application Protocol Data Unit (APDU), or a vector of APDUs to the network.

### Remarks ###
Network delivery of APDUs can be interspersed with ambient SPM and RDATA packets as necessary.

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
<td><tt>vector</tt> is one APDU if set <tt>TRUE</tt>, <tt>vector::iov_base</tt> is one APDU if set <tt>FALSE</tt>.</td>
</tr><tr>
<td><tt>bytes_written</tt></td>
<td>Pointer to store count of bytes written into <tt>buf</tt>.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>PGM_IO_STATUS_NORMAL</tt>, on error returns <tt>PGM_IO_STATUS_ERROR</tt>, on reset due to unrecoverable data loss, returns <tt>PGM_IO_STATUS_RESET</tt>.  If the transport is marked non-blocking then <tt>PGM_IO_STATUS_WOULD_BLOCK</tt> is returned if the operation would block, if the block would be caused by the rate limiting engine then <tt>PGM_IO_STATUS_RATE_LIMITED</tt> is returned instead.

### Example ###
Send one APDU covering two vector buffers.

```
 struct pgm_iovec vector[2];
 vector[0].iov_base = "hello ";
 vector[0].iov_len = strlen("hello ");
 vector[1].iov_base = "world!";
 vector[1].iov_len = strlen("world!") + 1;
 pgm_sendv (transport, vector, G_N_ELEMENTS(vector), TRUE, NULL);
```

Send two APDUs, one in English, one in Japanese.

```
 struct pgm_iovec vector[2];
 vector[0].iov_base = "hello world!";
 vector[0].iov_len = strlen("hello world!") + 1;
 vector[1].iov_base = "おはようございます";
 vector[1].iov_len = wcslen("おはようございます") + 1;  /* bytes not characters */
 pgm_sendv (transport, vector, G_N_ELEMENTS(vector), FALSE, NULL);
```

Send with non-blocking APDU with basic event detection.

```
 const char* buffer = "안녕하세요, 잘 지내 시죠?";
 struct pgm_iovec vector[2];
 PGMIOStatus status;
 gsize bytes_sent;
 vector[0].iov_base = buffer;
 vector[0].iov_len = wcslen(buffer) / 2;
 vector[1].iov_base = buffer + (wcslen(buffer) / 2);
 vector[1].iov_len = wcslen(buffer) - (wcslen(buffer) / 2);
 for(;;) {
   status = pgm_sendv (transport, vector, G_N_ELEMENTS(vector), TRUE, &bytes_sent);
   if (PGM_IO_STATUS_WOULD_BLOCK == status) {
     int n_fds = 3;
     struct pollfd fds[ n_fds ];
     memset (fds, 0, sizeof(fds));
     pgm_transport_poll_info (transport, fds, &n_fds, POLLIN);
     poll (fds, n_fds, -1 /* timeout=∞ */);
   } else {
     break;
   }
 }
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmIoStatus.md'>PGMIOStatus</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmSend.md'>pgm_send()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmSendSkbv.md'>pgm_send_skbv()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.<br>
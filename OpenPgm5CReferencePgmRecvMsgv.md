#summary OpenPGM 5 : C Reference : Socket : pgm\_recvmsgv()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_recvmsgv* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]*       sock,<br>
[OpenPgm5CReferencePgmMsgvT pgm_msgv_t]* const msgv,<br>
size_t            count,<br>
int               flags,<br>
size_t*           bytes_read,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**     error<br>
);<br>
</pre>

### Purpose ###
Receive a vector of Application Protocol Domain Unit's (APDUs) from the transport and drive the PGM receive and send state machines.

### Remarks ###
The PGM protocol is bi-directional, even send-only applications need to process incoming requests for retransmission (NAKs) and other packet types.  The synchronous API suite provides low level access to the raw events driving the protocol in order to accelerate performance in high message rate environments.

<tt>pgm_recvmsgv()</tt> fills a vector of <tt>pgm_msgv_t</tt> structures with a scatter/gather vector of buffers directly from the receive window.  The vector size is governed by IOV\_MAX, on Linux is 1024, therefore up to 1024 TPDUs or 1024 messages may be returned, whichever is the lowest.  Using the maximum size is not always recommended as time processing the received messages might cause an incoming buffer overrun.

The SKB objects are reference counted, to keep ownership of the memory increment the counter before the next call or transport destruction.

Unrecoverable data loss will cause the function to immediately return with <tt>PGM_IO_STATUS_RESET</tt>, setting flags to <tt>MSG_ERR_QUEUE</tt> will return a populated <tt>msgv</tt> with an error skbuff detailing the error.  If <tt>PGM_ABORT_ON_RESET</tt> is set false (the default mode) processing can continue with subsequent calls to <tt>pgm_recv()</tt>, if <tt>true</tt> then the transport will return <tt>PGM_IO_STATUS_RESET</tt> until destroyed.

It is valid to send and receive zero length PGM packets.

The <tt>pgm_recvmsgv()</tt> API and other versions are the entry into the core PGM state machine. Frequent calls must be made for send-only sockets in order to process incoming NAKs, send NCFs, send repair data (RDATA), in addition to sending broadcast SPMs to define the PGM tree.

Frequent calls must be made to <tt>pgm_recvmsgv()</tt> for receive-only sockets to generate and re-generate NAKs for missing data, and to send SPM-Request messages to new senders to expedite learning of their NLAs.


### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>sock</tt></td>
<td>The PGM socket object.</td>
</tr><tr>
<td><tt>msgv</tt></td>
<td>Message vector of TPDU vectors.</td>
</tr><tr>
<td><tt>count</tt></td>
<td>Elements in <tt>msgv</tt>.</td>
</tr><tr>
<td><tt>flags</tt></td>
<td>
<dl><dt><tt>MSG_DONTWAIT</tt></dt><dd>Enables non-blocking operation on Unix, no action on Windows.<br>
</dd><dt><tt>MSG_ERR_QUEUE</tt></dt><dd>Returns an error SKB on session reset.</dd></dl></td>
</tr><tr>
<td><tt>bytes_read</tt></td>
<td>Pointer to store count of bytes read into <tt>msgv</tt>.</td>
</tr><tr>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, returns <tt>PGM_IO_STATUS_NORMAL</tt>, on error returns <tt>PGM_IO_STATUS_ERROR</tt>, on reset due to unrecoverable data loss, returns <tt>PGM_IO_STATUS_RESET</tt>.  If the transport is marked non-blocking and no senders have been discovered then <tt>PGM_IO_STATUS_WOULD_BLOCK</tt> is returned if the operation would block.  If no data is available but a pending state timer is running <tt>PGM_IO_STATUS_TIMER_PENDING</tt> is returned.  If the state engine is trying to transmit repair data but is subject to the rate limiting engine then <tt>PGM_IO_STATUS_RATE_LIMITED</tt> is returned instead.

### Example ###
Read a maximum size vector from a transport.

```
 const unsigned iov_count = 10;
 unsigned iov_max = sysconf( SC_IOV_MAX );
 assert (iov_count <= iov_max);
 pgm_msgv_t msgv[ iov_count ];
 size_t bytes_read;
 pgm_recvmsgv (sock, msgv, iov_count, 0, &bytes_read, NULL);
```

Display error details on unrecoverable error.

```
 pgm_msgv_t msgv[10];
 size_t bytes_read;
 pgm_error_t* err = NULL;
 if (PGM_IO_STATUS_RESET == pgm_recvmsgv (sock, msgv, 10, 0, &bytes_read, &err)) {
   fprintf (stderr, "recv: %s",
            (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

Take ownership of a PGM SKB by incrementing the reference count with <tt><a href='OpenPgm5CReferencePgmAllocSkb.md'>pgm_skb_get()</a></tt>.

```
 pgm_msgv_t msgv[1];
 struct pgm_sk_buff_t* skb = NULL;
 if (PGM_IO_STATUS_NORMAL == pgm_recvmsgv (sock, msgv, 1, 0, NULL, NULL))
 {
   /* this is a message we are interested in */
   if (msgv[0].msgv_skb[0]->len > 1 &&
       msgv[0].msgv_skb[0]->data[0] == MAGIC_VALUE)
   {
     /* take reference */
     skb = pgm_skb_get (msgv[0].msgv_skb[0];
   }
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmIoStatus.md'>PGMIOStatus</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmRecv.md'>pgm_recv()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.
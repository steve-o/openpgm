#summary OpenPGM 5 : C Reference : Socket : pgm\_bind()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_bind* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]*                        restrict sock,<br>
const struct [OpenPgm5CReferencePgmSockAddrT pgm_sockaddr_t]* const restrict sockaddr,<br>
const socklen_t                             sockaddrlen,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**                      restrict error<br>
);<br>
<br>
bool *pgm_bind3* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]*                        restrict sock,<br>
const struct [OpenPgm5CReferencePgmSockAddrT pgm_sockaddr_t]* const restrict sockaddr,<br>
const socklen_t                             sockaddrlen,<br>
const struct group_req*            restrict send_req,<br>
const socklen_t                             send_req_len,<br>
const struct group_req*            restrict recv_req,<br>
const socklen_t                             recv_req_len,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**                      restrict error<br>
);<br>
</pre>

### Purpose ###
Bind a transport to the specified network devices.

### Remarks ###
Assigns local addresses to the PGM transport sockets which will initiate delivery of reconstructed messages to the asynchronous queue and its associated dispatcher threads.

Note not all operating systems segregate packets from different multicast group addresses as different streams to joined applications.  It is recommended in heterogenous environments to select unique multicast group address together with data-destination and when necessary UDP encapsulation ports, e.g. 239.192.0.1:7501, 239.192.0.2:7502.

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
<td><tt>sockaddr</tt></td>
<td>The PGM socket address to bind.</td>
</tr><tr>
<td><tt>sockaddrlen</tt></td>
<td>The size of the <tt>sockaddr</tt> parameter.</td>
</tr><tr>
<td><tt>send_req</tt></td>
<td>Interface to bind for sending.</td>
</tr><tr>
<td><tt>send_req_len</tt></td>
<td>The size of the <tt>send_req</tt> parameter.</td>
</tr><tr>
<td><tt>recv_req</tt></td>
<td>Interface to bind for receiving.</td>
</tr><tr>
<td><tt>recv_req_len</tt></td>
<td>The size of the <tt>recv_req</tt> parameter.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.


### Errors ###
The following are general socket errors only. There may be other platform-specific error codes.

**PGM\_ERROR\_DOMAIN\_SOCKET, PGM\_ERROR\_FAILED,**
  * _"Invalid maximum TPDU size."_
    * Common to both send and receive side sockets.
  * _"SPM ambient interval not configured."_
  * _"SPM heartbeat interval not configured."_
  * _"TXW\_SQNS not configured."_
  * _"TXW\_MAX\_RTE not configured."_
    * Send-side options are incomplete or invalid.
  * _"RXW\_SQNS not configured."_
  * _"RXW\_MAX\_RTE not configured."_
  * _"Peer timeout not configured."_
  * _"SPM-Request timeout not configured."_
  * _"NAK\_BO\_IVL not configured."_
  * _"NAK\_RPT\_IVL not configured."_
  * _"NAK\_RDATA\_IVL not configured."_
  * _"NAK\_DATA\_RETRIES not configured."_
  * _"NAK\_NCF\_RETRIES not configured."_
    * Receive-side options are incomplete or invalid.
  * _"Creating ACK notification channel: %s"_
  * _"Creating RDATA notification channel: %s"_
  * _"Creating waiting peer notification channel: %s"_
    * Resource shortage such as insufficient socket handles or file descriptors.
  * _"Binding receive socket to address %s: %s"_
  * _"Binding send socket to address %s: %s"_
  * _"Binding IP Router Alert (RFC 2113) send socket to address %s: %s"_
    * A platform specific error such as address conflict or insufficient privileges.

**PGM\_ERROR\_DOMAIN\_IF,**
  * _"Enumerating network interfaces: %s"_
    * An operating system error or resource shortage.  Typically a socket needs to be created to enumerate the systems interfaces and socket handle or file descriptor shortage will cause a failure.

**PGM\_ERROR\_DOMAIN\_IF, PGM\_ERROR\_NODEV,**
  * _"No matching network interface index: %i"_
    * The interface specified for binding no longer exists, such as a user unplugging a USB WiFi adapter.


### Example ###
Example binding to a fully specified interface set with a PGM/UDP socket and IPv4 addressing.
```
pgm_sock_t *sock = NULL;
struct pgm_sockaddr_t addr;
struct pgm_interface_req_t if_req;
pgm_error_t *err = NULL;
if (!pgm_bind3 (sock,
                &addr, sizeof (addr),
                &if_req, sizeof (if_req),        /* tx interface */
                &if_req, sizeof (if_req),        /* rx interface */
                &err))
{
  fprintf (stderr, "Binding PGM socket: %s\n",
           (err && err->message) ? err->message : "(null)");
  pgm_error_free (err);
  return EXIT_FAILURE;
}
```

Producing the following output:
```
Trace: Assuming IP header size of 20 bytes
Trace: Assuming UDP header size of 8 bytes
Trace: Binding receive socket to INADDR_ANY
Trace: Binding send socket to interface index 2
```


### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmSockAddrT.md'>pgm_sockaddr_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
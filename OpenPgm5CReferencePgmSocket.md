#summary OpenPGM 5 : C Reference : Socket : pgm\_socket()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_socket* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]**  restrict sock,<br>
const sa_family_t      family,<br>
const int              sock_type,<br>
const int              protocol,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]** restrict error<br>
);<br>
</pre>

### Purpose ###
Create a PGM socket.

### Remarks ###
A custom network protocol requires super-user privileges to open the necessary raw sockets.  An application can call <tt>pgm_socket()</tt> to create unbound raw sockets, drop the privileges to maintain security, and then bind with <tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt> and finally <tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt> to start processing incoming packets.

Not all networking hardware permits IP Router Alert tagged packets to pass through, tagging may be disabled with the PGM\_IP\_ROUTER\_ALERT socket option.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>sock</tt></td>
<td>Location to store the PGM socket object.</td>
</tr><tr>
<td><tt>family</tt></td>
<td>The desired IP address family, <tt>AF_INET</tt> or <tt>AF_INET6</tt>.</td>
</tr><tr>
<td><tt>sock_type</tt></td>
<td>Socket type, must be <tt>SOCK_SEQPACKET</tt>.</td>
</tr><tr>
<td><tt>protocol</tt></td>
<td>The protocol layer to use, <tt>IPPROTO_PGM</tt> or <tt>IPPROTO_UDP</tt>.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.


### Errors ###
The following are general socket errors only. There may be other platform-specific error codes.

**PGM\_ERROR\_DOMAIN\_SOCKET,**
  * _"Creating receive socket: %s(%d)"_
    * Resource shortage or insufficient privileges to create the socket.
  * _"Creating send socket: %s"_
  * _"Creating IP Router Alert (RFC 2113) send socket: %s"_
    * Resource shortage such as insufficient socket handles or file descriptors.
  * _"Enabling reuse of socket local address: %s"_
  * _"Enabling reuse of duplicate socket address and port bindings: %s"_
  * _"Enabling receipt of ancillary information per incoming packet: %s"_
  * _"Enabling IP header in front of user data: %s"_
  * _"Enabling receipt of control message per incoming datagram: %s"_
    * Platform specific issue such as permissioning or system configuration.


### Example ###
Create a PGM/UDP transport with IPv6 addressing.
```
 pgm_sock_t *sock = NULL;
 pgm_error_t *err = NULL;
 if (!pgm_socket (&sock, AF_INET6, SOCK_SEQPACKET, IPPROTO_UDP, &err)) {
   fprintf (stderr, "Creating PGM/UDP socket: %s\n",
            (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

Producing the following output:
```
Trace: Opening UDP encapsulated sockets.
Trace: Set socket sharing.
Trace: Request socket packet-info.
```


### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
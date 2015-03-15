_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_if_parse_transport* (<br>
const char*                  network,<br>
int                          ai_family,<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]*     recv_gsr,<br>
gsize*                       recv_len,<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]*     send_gsr<br>
);<br>
</pre>

### Purpose ###
Decompose a string network specification.

### Remarks ###
The TIBCO Rendezvous network parameter provides a convenient compact representation of the complicated data structures needed to specify sending and receiving interfaces and multicast addresses.  This function provides conversion between the network string and the parameters of <tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt>.

Both <tt>recv_gsr</tt> and <tt>send_gsr</tt> are populated as per RFC 3678, <tt>gsr_interface</tt> is the index of the link layer interface, <tt>gsr_group</tt> is the multicast address.  By design, <tt>gsr_source</tt> is a copy of <tt>gsr_group</tt>, this indicates any-source multicast (ASM).

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>network</tt></td>
<td>A TIBCO Rendezvous compatible network parameter string</td>
</tr><tr>
<td><tt>ai_family</tt></td>
<td>The target address family, either <tt>AF_UNSPEC</tt>, <tt>AF_INET</tt>, or <tt>AF_INET6</tt>.</td>
</tr><tr>
<td><tt>recv_gsr</tt></td>
<td>Location to store the receive interface, multicast group, and source address.</td>
</tr><tr>
<td><tt>recv_len</tt></td>
<td>The number of interface & group pairs that can be stored in <tt>recv_gsr</tt>, returns the actual number of pairs parsed from the <tt>network</tt> string.</td>
</tr><tr>
<td><tt>send_gsr</tt></td>
<td>Location to store the sending interface and multicast group.  Source address is ignored, so effectively only a <a href='http://www.ietf.org/rfc/rfc3678.txt'>group_req</a> structure.</td>
</tr>
</table>


### Return Value ###
On success, 0 is returned.  On invalid arguments, <tt>-EINVAL</tt> is returned.  If more multicast groups are found than the <tt>recv_len</tt> parameter, <tt>-ENOMEM</tt> is returned.

### Example ###
```
 char* network = "eth0;226.0.0.1";
 struct group_source_req recv_gsr, send_gsr;
 gsize recv_len = 1;
 
 pgm_if_parse_transport (network, AF_INET, &recv_gsr, &recv_len, &send_gsr);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
<ul><li><tt><a href='http://www.ietf.org/rfc/rfc3678.txt'>group_source_req</a></tt> in RFC 3678<br>
</li><li><a href='http://en.wikipedia.org/wiki/Any-source_multicast'>Any-source multicast (ASM)</a> in Wikipedia.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Source-specific_multicast'>Source-source multicast (SSM)</a> in Wikipedia.
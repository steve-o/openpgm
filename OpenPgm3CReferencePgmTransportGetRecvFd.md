#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_get\_recv\_fd()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_get_recv_fd* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport<br>
);<br>
<br>
int *pgm_transport_get_pending_fd* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport<br>
);<br>
<br>
int *pgm_transport_get_repair_fd* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport<br>
);<br>
<br>
int *pgm_transport_get_send_fd* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const    transport<br>
);<br>
</pre>

### Purpose ###
Get event notification file descriptors.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr>
</table>

### Return Value ###
On success, returns requested file descriptor.


### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmSelectInfo.md'>pgm_transport_select_info()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmPollInfo.md'>pgm_transport_poll_info()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
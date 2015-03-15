#summary OpenPGM 5 : C Reference : Socket : pgm\_getsockopt()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_getsockopt* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const restrict sock,<br>
const int                  level,<br>
const int                  optname,<br>
void*             restrict optval,<br>
socklen_t*        restrict optlen<br>
);<br>
<br>
bool *pgm_setsockopt* (<br>
[OpenPgm5CReferencePgmSockT pgm_sock_t]* const restrict sock,<br>
const int                  level,<br>
const int                  optname,<br>
const void*       restrict optval,<br>
socklen_t                  optlen<br>
);<br>
</pre>

### Purpose ###
Get and set options on PGM sockets.

### Remarks ###
<tt>pgm_getsockopt()</tt> and <tt>pgm_setsockopt()</tt> manipulate options for the PGM socket referred to by <tt>sock</tt>.

When manipulating socket options, the level at which the option resides and the name of the option must be specified.  To manipulate options at the sockets API level, level is specified as <tt>SOL_SOCKET</tt>.  To manipulate options for the PGM protocol, level is specified as <tt>IPPROTO_PGM</tt>.

The arguments <tt>optval</tt> and <tt>optlen</tt> are used to access option values for <tt>pgm_setsockopt()</tt>.  For
<tt>pgm_getsockopt()</tt> they identify a buffer in which the value for the requested option(s) are to be returned.
For <tt>pgm_getsockopt()</tt>, <tt>optlen</tt> is a value-result argument, initially containing the size of the buffer
pointer to by <tt>optval</tt>, and modified on return to indicate the actual size of the value returned.

Most socket-level options utilise an <tt>int</tt> argument for <tt>optval</tt>.

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
<td><tt>level</tt></td>
<td>API level.</td>
</tr><tr>
<td><tt>optname</tt></td>
<td>Option name.</td>
</tr><tr>
<td><tt>optval</tt></td>
<td>Option value.</td>
</tr><tr>
<td><tt>optlen</tt></td>
<td>The size of the <tt>optval</tt> parameter.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On invalid parameter, returns <tt>false</tt>.


### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSockT.md'>pgm_sock_t</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
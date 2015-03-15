#summary OpenPGM : C Reference : pgm\_transport\_bind()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_transport_bind* (<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t]* const    transport<br>
);<br>
</pre>

### Purpose ###
Bind a transport to the specified network devices.

### Remarks ###
Assigns local addresses to the PGM transport sockets which will initiate delivery of reconstructed messages to the asynchronous queue and its associated dispatcher threads.

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
On success, returns 0.  If <tt>transport</tt> is invalid or already bound, returns <tt>-EINVAL</tt>.  On error, returns -1 and sets <tt>errno</tt> appropriately, or if the local interface cannot be resolved, -2 is returned <tt>h_errno</tt> set appropriately.

### Errors ###
The variable <tt>errno</tt> can have the following values:

<dl><dt><tt>EACCES</tt></dt><dd>The address is protected, and the user is not the superuser.<br>
</dd><dt><tt>EADDRINUSE</tt></dt><dd>The given address is already in use.<br>
</dd><dt><tt>EADDRNOTAVAIL</tt></dt><dd>Unknown multicast group address passed.<br>
</dd><dt><tt>EMFILE</tt></dt><dd>Too many file descriptors are in use by the process.<br>
</dd><dt><tt>ENFILE</tt></dt><dd>The system limit on the total number of open files has been reached.<br>
The variable <tt>h_errno</tt> can have the following values:<br>
</dd><dt><tt>HOST_NOT_FOUND</tt></dt><dd>The specified host is unknown.<br>
</dd><dt><tt>NO_ADDRESS</tt> or <tt>NO_DATA</tt></dt><dd>The requested name is valid but does not have an IP address.<br>
</dd><dt><tt>NO_RECOVERY</tt></dt><dd>A non-recoverable name server error occurred.<br>
</dd><dt><tt>TRY_AGAIN</tt></dt><dd>A temporary error occurred on an authoritative name server.  Try again later.<br>
</dd></dl>

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
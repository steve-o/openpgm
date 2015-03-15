#summary OpenPGM : C Reference : pgm\_async\_create()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_async_create* (<br>
[OpenPgmCReferencePgmAsyncT pgm_async_t]**             async,<br>
[OpenPgmCReferencePgmTransportT pgm_transport_t] const*    transport,<br>
int                       preallocate<br>
);<br>
</pre>

### Purpose ###
Create an asynchronous event handler.

### Remarks ###
APDUs are stored in an asynchronous queue using event objects, the <tt>preallocate</tt> parameter is used to set the initial number of these objects to keep in the trash stack.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>async</tt></td>
<td>The asynchronous receiver object.</td>
</tr><tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>preallocate</tt></td>
<td>Number of event objects.</td>
</tr>
</table>


### Return Value ###
On success, 0 is returned.  On invalid arguments, a critical message is logged and <tt>-EINVAL</tt> is returned.  On error, -1 is returned, and <tt>errno</tt> is set appropriately.

### Errors ###
<dl><dt><tt>EAGAIN</tt></dt><dd>The system lacked the necessary resources to create another thread, or the system-imposed limit on the total number of threads in a process <tt>PTHREAD_THREADS_MAX</tt> would be exceeded.<br>
</dd><dt><tt>EMFILE</tt></dt><dd>Too many file descriptors are in use by the process.<br>
</dd><dt><tt>ENFILE</tt></dt><dd>The system limit on the total number of open files has been reached.<br>
</dd></dl>

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
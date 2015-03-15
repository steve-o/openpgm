#summary OpenPGM 2 : C Reference : Asynchronous Receiver : pgm\_async\_get\_fd()
#labels Phase-Implementation
#sidebar TOC2CReferenceAsynchronousReceiver
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_async_get_fd* (<br>
[OpenPgm2CReferencePgmAsyncT pgm_async_t]*    async<br>
);<br>
</pre>

### Purpose ###
Retrieve file descriptor for event signalling.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>async</tt></td>
<td>The asynchronous receiver object.</td>
</tr>
</table>


### Return Value ###
On success the file descriptor is returned.  If <tt>async</tt> is invalid, <tt>-EINVAL</tt> is returned.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.
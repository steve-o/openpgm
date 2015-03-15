#summary OpenPGM : C Reference : pgm\_async\_destroy()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_async_destroy* (<br>
[OpenPgmCReferencePgmAsyncT pgm_async_t]**    async<br>
);<br>
</pre>

### Purpose ###
Destroy an asynchronous event handler.

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
On success, 0 is returned.  If <tt>async</tt> is invalid, <tt>-EINVAL</tt> is returned.

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt><br>
</li><li><tt><a href='OpenPgmCReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
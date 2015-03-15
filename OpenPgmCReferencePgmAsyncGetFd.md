#summary OpenPGM : C Reference : pgm\_async\_get\_fd()
#labels Phase-Implementation

_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
int *pgm_async_get_fd* (<br>
[OpenPgmCReferencePgmAsyncT pgm_async_t]*    async<br>
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
<td>Asynchronous receiver object.</td>
</tr>
</table>


### Return Value ###
On success the file descriptor is returned.  If <tt>async</tt> is invalid, <tt>-EINVAL</tt> is returned.

### Example ###
```
 int fd = pgm_async_get_fd (async);
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmAsyncT.md'>pgm_async_t</a></tt><br>
<ul><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
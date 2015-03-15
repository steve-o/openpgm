### Introduction ###
An asynchronous receiver is a transport that queues incoming messages for latter processing.

### Topics in Alphabetical Order ###
<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Function or Type</th>
<th>Description</th>
</tr>
<tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncT.md'>pgm_async_t</a></tt></td>
<td>Object representing a transport asynchronous receiver.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmEventFnT.md'>pgm_eventfn_t</a></tt></td>
<td>Callback function pointer for asynchronous events.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt></td>
<td>Create an asynchronous event handler.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncDestroy.md'>pgm_async_destroy()</a></tt></td>
<td>Destroy an asynchronous event handler.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncGetFd.md'>pgm_async_get_fd()</a></tt></td>
<td>Retrieve file descriptor for event signalling.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncSetNonBlocking.md'>pgm_async_set_nonblocking()</a></tt></td>
<td>Set asynchronous operation to blocking or non-blocking mode.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch()</a></tt></td>
<td>Add a transport event listener.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch_full()</a></tt></td>
<td>Add a transport event listener, and run a completion function when all of the destroyed event’s callback functions are complete.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncCreateWatch.md'>pgm_async_create_watch()</a></tt></td>
<td>Create a transport event listener.</td>
</tr><tr>
<td><tt><a href='OpenPgm2CReferencePgmAsyncRecv.md'>pgm_async_recv()</a></tt></td>
<td>Synchronous receiving from an asynchronous queue.</td>
</tr>
</table>
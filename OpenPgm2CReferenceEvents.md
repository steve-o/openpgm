#summary OpenPGM 2 : C Reference : Events
#labels Phase-Implementation
#sidebar TOC2CReferenceEvents

NAK and NCF processing requires that one thread is always processing incoming events.  With a synchronous strategy this should be one dedicated application thread reading messages and copying or processing into an internal queue, with asynchronous event handling a managed thread can be created with <tt><a href='OpenPgm2CReferencePgmAsyncCreate.md'>pgm_async_create()</a></tt>.

Event driven programs idle in an event loop dispatching events as they are raised.  OpenPGM application developers can choose whether to follow an event driven model or regular imperative styles.

Events can come from any number of different types of sources such as file descriptors (plain files, pipes or sockets) and timeouts.  With OpenPGM events can be driven from the underlying network transport, timing state engine, or at a higher level from an asynchronous data queue.

When relying on the underlying network transport for event generation the developer can detect level events with POSIX functions <tt>select</tt>, <tt>poll</tt>, or higher speed with edge levels via <tt>epoll</tt>.  The following table provides a summary of the APIs that allow integration with such mechanisms:

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Synchronisation Strategy</th>
<th>Event Mechanism</th>
<th>OpenPGM API</th>
</tr>
<tr>
<td>Synchronous</td>
<td><tt>select()</tt></td>
<td><tt><a href='OpenPgm2CReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt></td>
</tr><tr>
<td><tt>poll()</tt></td>
<td><tt><a href='OpenPgm2CReferencePgmTransportPollInfo.md'>pgm_transport_poll_info()</a></tt></td>
</tr><tr>
<td><tt>epoll()</tt></td>
<td><tt><a href='OpenPgm2CReferencePgmTransportEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt></td>
</tr><tr>
<td>Asynchronous</td>
<td><tt>select()</tt></td>
<td><tt><a href='OpenPgm2CReferencePgmAsyncGetFd.md'>pgm_async_get_fd()</a></tt></td>
</tr><tr>
<td><tt>poll()</tt></td>
</tr><tr>
<td><tt>epoll()</tt></td>
</tr><tr>
<td>GLib Event Source</td>
<td><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_async_add_watch()</a></tt></td>
</tr>
</table>


Note that the special APIs for synchronous events as multiple file descriptors need to be monitored for correct operation, see the section [Data Triggers](OpenPgmConceptsEvents.md) in _OpenPGM Concepts_ for further details.


## Asynchronous Threading ##
To allow multiple independent sets of sources to be handled in different threads, GLib Event Sources are associated with a [GMainContext](http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GMainContext). A [GMainContext](http://library.gnome.org/devel/glib/stable/glib-The-Main-Event-Loop.html#GMainContext) can only be running in a single thread, but sources can be added to it and removed from it from other threads.

To have one than one thread processing incoming messages GLib provides [thread pools](http://library.gnome.org/devel/glib/stable/glib-Thread-Pools.html).  An added advantage is, that the threads can be shared between the different subsystems of your program.
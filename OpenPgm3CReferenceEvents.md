#summary OpenPGM 3 : C Reference : Events
#labels Phase-Implementation
#sidebar TOC3CReferenceEvents
NAK and NCF processing requires that one thread is always processing incoming events.  With a synchronous strategy this should be one dedicated application thread reading messages and copying or processing into an internal queue for on-demand processing.

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
<td><tt><a href='OpenPgm3CReferencePgmTransportSelectInfo.md'>pgm_transport_select_info()</a></tt></td>
</tr><tr>
<td><tt>poll()</tt></td>
<td><tt><a href='OpenPgm3CReferencePgmTransportPollInfo.md'>pgm_transport_poll_info()</a></tt></td>
</tr><tr>
<td><tt>epoll()</tt></td>
<td><tt><a href='OpenPgm3CReferencePgmTransportEpollCtl.md'>pgm_transport_epoll_ctl()</a></tt></td>
</tr>
</table>


Note that the special APIs for synchronous events as multiple file descriptors need to be monitored for correct operation, see the section [Data Triggers](OpenPgmConceptsEvents.md) in _OpenPGM Concepts_ for further details.

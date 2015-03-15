OpenPGM is a flexible messaging API which allows the developer to choose between high speed edge-level polling or slower but conceptually friendly event-driven callbacks.

<img src='http://miru.hk/wiki/OpenPGM_Events.png' />

Polling is necessary in very high speed environments as the system buffers constantly contain data.  Edge-level polling is a convenient method of reducing the number of generated events allowing the application more time to actually process the messages.


## Data Triggers ##
There are two sources for alerting receivers to the presence of incoming data to process.  One is the network socket for network layer events, and the other is an internal pipe for receive state expiration.

<img src='http://miru.hk/wiki/PGM_events.png' />

Normally you will only see events on the network layer source, expiration sourced data occurs when contiguous data is waiting to be committed:

<img src='http://miru.hk/wiki/Loss_detection_by_gap.png' />

Imagine packets 1 & 2 have been successfully received and passed to the application, packet 3 was lost, but packets 4, 5 & 6 are queued up waiting to be committed.  PGM will send NAKs to the sender until the lost packet has been received or <tt>NAK_DATA_RETRIES</tt> or <tt>NAK_NCF_RETRIES</tt> has been exceeded.  When those limits have been the exceeded PGM can commit packets 4, 5 & 6.  There is no incoming data creating the event, the trigger is a timer to notify the application.


## Asynchronous Events ##

Program callback functions respond to asynchronous events, such as an inbound message or a timer event.  Event handling examples are provided by the GLib API, but any event manager interface can be used.

This section presents events and related concepts.

  * [Listener Event Semantics](OpenPgmConceptsListenerEventSemantics.md)
  * [Timer Event Semantics](OpenPgmConceptsTimerEventSemantics.md)

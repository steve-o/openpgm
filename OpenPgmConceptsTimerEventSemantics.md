Millisecond level timers are handled by the platforms LIBC library, high resolution timers for intervals less than a millisecond require direct polling of the core and its associated internal clock.

<img src='http://miru.hk/wiki/OpenPGM_stack.png' />

Timer events are generated at repeating intervals, there is a overhead between the actual interval time and the callback time which can vary with the number of timers being processed.  A typical timer event timeline looks as follows.

<img src='http://miru.hk/wiki/OpenPGM_timer_events.png' />

Using the GLib API note that the interval is calculated after the timer callback, so the actual timeline would be this.

<img src='http://miru.hk/wiki/OpenPGM_GLib_timer_events.png' />

The high resolution timing by OpenPGM is a best attempt at the first by using the latter with actual execution time offsets and microsecond accuracy.

<img src='http://miru.hk/wiki/OpenPGM_HR_timer_events.png' />

After the callback the second interval if elapsed would cause another callback, for performance reasons if multiple timer intervals have elapsed during the callback only one event is generated.

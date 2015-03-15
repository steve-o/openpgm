### Introduction ###
Warnings and errors in OpenPGM maybe raised via log messages or return status values.

### Logging ###
OpenPGM modules log messages with different levels of importance and functional roles.  The environmental variable <tt>PGM_MIN_LOG_LEVEL</tt> sets the minimum logging level as follows,

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Log Level</th>
<th>PGM_MIN_LOG_LEVEL</th>
</tr>
<tr>
<td><tt>PGM_LOG_LEVEL_DEBUG</tt></td>
<td><tt>DEBUG</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_TRACE</tt></td>
<td><tt>TRACE</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_MINOR</tt></td>
<td><tt>MINOR</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_NORMAL</tt></td>
<td><tt>NORMAL</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_WARNING</tt></td>
<td><tt>WARNING</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_ERROR</tt></td>
<td><tt>ERROR</tt></td>
</tr><tr>
<td><tt>PGM_LOG_LEVEL_FATAL</tt></td>
<td><tt>FATAL</tt></td>
</tr>
</table>

When the <tt>PGM_LOG_LEVEL_TRACE</tt> level is active the functional roles can be enabled by setting bit fields on the environmental variable <tt>PGM_LOG_MASK</tt>,

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Mask</th>
<th>Description</th>
</tr>
<tr>
<td><tt>0x1</tt></td>
<td>Log memory allocations.</td>
</tr><tr>
<td><tt>0x2</tt></td>
<td>Log events related to network input and timers.</td>
</tr><tr>
<td><tt>0x4</tt></td>
<td>Log changes to configuration.</td>
</tr><tr>
<td><tt>0x10</tt></td>
<td>Log activity associated with network connections.</td>
</tr><tr>
<td><tt>0x20</tt></td>
<td>Log NAK activity.</td>
</tr><tr>
<td><tt>0x40</tt></td>
<td>Log rate control activity.</td>
</tr><tr>
<td><tt>0x80</tt></td>
<td>Log activity associated with the sender transmission window.</td>
</tr><tr>
<td><tt>0x100</tt></td>
<td>Log activity associated with the receive window.</td>
</tr><tr>
<td><tt>0x400</tt></td>
<td>Log encoding and decoding of FEC pro-active parity packets.</td>
</tr><tr>
<td><tt>0x800</tt></td>
<td>Log activity associated with congestion control.</td>
</tr><tr>
<td><tt>0xfff</tt></td>
<td>Log all activity.</td>
</tr>
</table>

Log messages should be redirected using <tt>pgm_log_set_handler()</tt>, and can then be easily integrated with other logging systems.

### Example ###
```
 static const char log_levels[8][6] = {
        "Uknown",
        "Debug",
        "Trace",
        "Minor",
        "Info",
        "Warn",
        "Error",
        "Fatal"
 };
 
 static void
 log_handler (
        const int         log_level,
        const char`*`       message,
        void`*`              closure
        )
 {
     printf ("%s %s\n", log_levels[log_level], message);
 }
 
 ...
 
 pgm_log_set_handler (log_handler, NULL);
```
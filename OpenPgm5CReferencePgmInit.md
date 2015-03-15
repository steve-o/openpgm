_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_init* ([OpenPgm5CReferencePgmErrorT pgm_error_t]**     error);<br>
</pre>

### Purpose ###
Create and start OpenPGM internal machinery.

### Remarks ###
This call creates the internal machinery that OpenPGM requires for its operation:

  * Messaging support.
  * Thread synchronisation.
  * Memory management.
  * Pseudo random number generator.
  * Timer support.
  * PGM protocol number.

Until the first call to <tt>pgm_init()</tt>, all events, queues, and transports are unusable.

<tt>pgm_init()</tt> implements reference counting, every call needs to be matched with a call to <tt><a href='OpenPgm5CReferencePgmShutdown.md'>pgm_shutdown()</a></tt>. If you want to make sure that the PGM engine is initialized without references, you can do this:

```
 if (!pgm_supported ()) pgm_init (NULL);
```

The number of CPUs will be enumerated on the system, if it is detected that only one core is available to the process then busy-wait loops will perform a thread yield or equivalent.  The number of cores can be restricted as per the operating systems process affinity settings, but changing the number of bound cores after calling <tt>pgm_init()</tt> will not update the busy-wait handling.

Memory management for running under a profiler such as Valgrind is configurable via <tt><a href='OpenPgm5CReferenceEnvironment.md'>PGM_DEBUG</a></tt> environment variable.  Set to <tt>gc-friendly</tt> to ensure all allocated memory that isn't directly initialized will be reset to 0.

On Microsoft Windows platforms <tt>pgm_init()</tt> will request 1ms minimum resolution timers via the <tt><a href='http://msdn.microsoft.com/en-us/library/dd757624(v=vs.85).aspx'>timeBeginPeriod()</a></tt> function.  The resolution period is relinquished on shutdown of the PGM engine via <tt><a href='OpenPgm5CReferencePgmShutdown.md'>pgm_shutdown()</a></tt>.

The timer mechanism is configurable via the <tt><a href='OpenPgm5CReferenceRunWithTheseCapabilities.md'>PGM_TIMER</a></tt> environment variable.

The PGM protocol number will be read from any system supported name services, for example <tt>/etc/protocols</tt>.  If not configured in the system, or the system does not support such a database the protocol number will default to 113.


### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>err</tt></td>
<td>a return location for a <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On failure, returns <tt>false</tt>.  If <tt>err</tt> is provided it may be set detailing any Winsock or timing device initialization issue.


### Errors ###
The following are general initialization errors only.  There may be other platform-specific error codes.

**PGM\_ERROR\_DOMAIN\_ENGINE, PGM\_ERROR\_FAILED,**
  * _"WSAStartup failure: %s"_
  * _"WSAStartup failed to provide requested version 2.2."_
  * _"WSARecvMsg function not found, available in Windows XP or Wine 1.3."_
    * Indicates an invalid operating system such as Windows 2000 or Wine 1.2 and earlier, including a possible broken WinSock subsystem.
  * _"Cannot open socket."_
    * Indicates a resource shortage of socket handles or file descriptors.

**PGM\_ERROR\_DOMAIN\_TIME, PGM\_ERROR\_FAILED,**
  * _"Unsupported time stamp function: PGM\_TIMER=%s"_
  * _"No supported high-resolution performance counter: %s"_
  * _"Cannot open /dev/rtc for reading: %s"_
  * _"Cannot set RTC frequency to %i Hz: %s"_
  * _"Cannot enable periodic interrupt (PIE) on RTC: %s"_
  * _"Cannot open /dev/hpet for reading: %s"_
  * _"Error mapping HPET device: %s"_
    * The configured or default timing mechanism is unavailable for operation.


### Example ###
```
 pgm_error_t *err = NULL;
 if (!pgm_init (&err)) {
   fprintf (stderr, "PGM Engine initialization error %s\n",
            (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

Producing the following output:
```
Minor: OpenPGM 5.2.119 (1422) 2011-10-06 20:56:05 Linux i686
Minor: Detected 2 available 2 online 2 configured CPUs.
Minor: Using gettimeofday() timer.
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSupported.md'>pgm_supported()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmShutdown.md'>pgm_shutdown()</a></tt><br>
</li><li><tt><a href='http://msdn.microsoft.com/en-us/library/dd757624(v=vs.85).aspx'>timeBeginPeriod()</a></tt> in MSDN Library.<br>
</li><li><a href='OpenPgm5CReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.
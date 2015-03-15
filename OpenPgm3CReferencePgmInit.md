_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_init* ([OpenPgm3CReferencePgmErrorT pgm_error_t]**     error);<br>
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

<tt>pgm_init()</tt> implements reference counting, every call needs to be matched with a call to [pgm\_shutdown()](OpenPgm3CReferencePgmShutdown.md). If you want to make sure that the PGM engine is initialized without references, you can do this:

```
 if (!pgm_supported ()) pgm_init (NULL);
```

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>err</tt></td>
<td>a return location for a <a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a>, or <tt>NULL</tt>.</td>
</tr>
</table>


### Return Value ###
On success, returns <tt>true</tt>.  On failure, returns <tt>false</tt>.  If <tt>err</tt> is provided it may be set detailing any Winsock or timing device initialization issue.


### Example ###
```
 pgm_error_t *err = NULL;
 if (!pgm_init (&err)) {
   fprintf (stderr, "PGM Engine initialization error %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmSupported.md'>pgm_supported()</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmShutdown.md'>pgm_shutdown()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm3CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.
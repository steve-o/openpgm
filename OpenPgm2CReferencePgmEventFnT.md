_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef int (**pgm_eventfn_t*)(gpointer buf, guint len, gpointer user_data);<br>
</pre>

### Purpose ###
Callback function pointer for asynchronous events.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_add_watch()</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmAsyncAddWatch.md'>pgm_add_watch_full()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceAsynchronousReceiver.md'>Asynchronous Receiver</a> in OpenPGM C Reference.
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
  * <tt><a href='OpenPgmCReferencePgmAsyncAddWatch.md'>pgm_add_watch()</a></tt><br>
<ul><li><tt><a href='OpenPgmCReferencePgmAsyncAddWatch.md'>pgm_add_watch_full()</a></tt><br>
</li><li><a href='OpenPgmCReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
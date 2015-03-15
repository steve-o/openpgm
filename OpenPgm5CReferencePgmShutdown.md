_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_shutdown* (void);<br>
</pre>

### Purpose ###
Shutdown OpenPGM internal machinery.

### Remarks ###
On Microsoft Windows platforms <tt>pgm_shutdown()</tt> will relinquish use of 1ms high resolution timers via the <tt><a href='http://msdn.microsoft.com/en-us/library/dd757626(v=vs.85).aspx'>timeEndPeriod()</a></tt> function.

### Return Value ###
On success, returns <tt>true</tt>.  If the library was not initialized with a call to <tt><a href='OpenPgm5CReferencePgmInit.md'>pgm_init()</a></tt>, or calls to <tt>pgm_shutdown()</tt> without a matching <tt>pgm_init()</tt> have occurred then the return value will be <tt>false</tt>.

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSupported.md'>pgm_supported()</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmInit.md'>pgm_init()</a></tt><br>
</li><li><tt><a href='http://msdn.microsoft.com/en-us/library/dd757626(v=vs.85).aspx'>timeEndPeriod()</a></tt> in MSDN Library.<br>
</li><li><a href='OpenPgm5CReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.
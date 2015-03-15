_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef guint64 *pgm_time_t*;<br>
<br>
#defined PGM_TIME_FORMAT G_GUINT64_FORMAT<br>
</pre>

### Purpose ###
Time value data type.

### Remarks ###
Depending on the runtime configuration <tt>pgm_time_t</tt> values may be absolute of offset to a particular time such as startup of the active core.  In order to calculate absolute time use <tt>pgm_time_since_epoch()</tt>.

### Example ###
Display literal time value.
```
 pgm_time_t a_time;
 g_message ("a_time is %" PGM_TIME_FORMAT, a_time);
```
Display absolute local time.
```
 pgm_time_t a_time;
 time_t epoch_time;
 struct tm local_time;
 char outstr[1024];
 pgm_time_since_epoch (&a_time, &epoch_time);
 localtime_r (&epoch_time, &local_time);
 strftime (outstr, sizeof(outstr), "%T", &local_time);
 g_message ("a_time is %s", outstr);
```

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTimeSinceEpoch.md'>pgm_time_since_epoch()</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceTiming.md'>Timing</a> in OpenPGM C Reference.
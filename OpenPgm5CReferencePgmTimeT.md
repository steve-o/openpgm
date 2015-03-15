_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef uint64_t *pgm_time_t*;<br>
<br>
#defined PGM_TIME_FORMAT PRIu64<br>
</pre>

### Purpose ###
Time value data type.

### Remarks ###
Depending on the runtime configuration <tt>pgm_time_t</tt> values may be absolute of offset to a particular time such as startup of the active core.  In order to calculate absolute time use <tt>pgm_time_since_epoch()</tt>.

### Example ###
Display literal time value.
```
 pgm_time_t a_time;
 printf ("a_time is %" PGM_TIME_FORMAT "\n", a_time);
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
 printf ("a_time is %s\n", outstr);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmTimeSinceEpoch.md'>pgm_time_since_epoch()</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceTiming.md'>Timing</a> in OpenPGM C Reference.
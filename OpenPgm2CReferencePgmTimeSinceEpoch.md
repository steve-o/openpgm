#summary OpenPGM : C Reference : Timing : pgm\_time\_since\_epoch()
#labels Phase-Implementation
#sidebar TOC2CReferenceTiming
_Function_
### Declaration ###
<pre>
#include <time.h><br>
#include <pgm/pgm.h><br>
<br>
void *pgm_time_since_epoch* (<br>
const [OpenPgm2CReferencePgmTimeT pgm_time_t]*    pgm_time_t_time,<br>
time_t*    time_t_time<br>
);<br>
</pre>

### Purpose ###
Convert <tt><a href='OpenPgm2CReferencePgmTimeT.md'>pgm_time_t</a></tt> to <tt>time_t</tt> scoped time.

### Remarks ###
Some timing sources use a counter based from the core or timing device boot time, other timing
> sources use a counting frequency different to the resolution provided by <tt><a href='OpenPgm2CReferencePgmTimeT.md'>pgm_time_t</a></tt>.  Calling <tt>pgm_time_since_epoch()</tt> will scale the timing source to microseconds and use an offset calculated at timer module startup.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>pgm_time_t_time</tt></td>
<td>OpenPGM scaled and offset time</td>
</tr><tr>
<td><tt>time_t_time</tt></td>
<td>POSIX.1 scaled and offset time, seconds since the Epoch (1970, January 1).</td>
</tr>
</table>


### Example ###
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
  * <tt><a href='OpenPgmCReferencePgmTimeT.md'>pgm_time_t</a></tt><br>
<ul><li><a href='OpenPgm2CReferenceTiming.md'>Timing</a> in OpenPGM C Reference.
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]* *pgm_skb_copy* (<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]*     skb<br>
);<br>
</pre>

### Purpose ###
Create a copy of a skbuff.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>skb</tt></td>
<td>PGM skbuff to copy.</td>
</tr>
</table>


### Example ###
Create a new PGM skbuff, copy it and free the original.

```
 struct pgm_sk_buff_t *skb, *copy;
 skb = pgm_alloc_skb (1500);
 copy = pgm_skb_copy (skb);
 pgm_free_skb (skb);
```

### See Also ###
  * <tt>struct <a href='OpenPgm2CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
</li><li><a href='OpenPgm2CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.</li></ul>

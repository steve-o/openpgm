_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct [OpenPgm5CReferencePgmSkBuffT pgm_sk_buff_t]* *pgm_alloc_skb* (<br>
uint16_t                   size<br>
);<br>
<br>
struct [OpenPgm5CReferencePgmSkBuffT pgm_sk_buff_t]* *pgm_skb_get* (<br>
struct [OpenPgm5CReferencePgmSkBuffT pgm_sk_buff_t]*     skb<br>
);<br>
<br>
void *pgm_free_skb* (<br>
struct [OpenPgm5CReferencePgmSkBuffT pgm_sk_buff_t]*     skb<br>
);<br>
</pre>

### Purpose ###
<tt>pgm_alloc_skb()</tt> creates a PGM socket buffer.

<tt>pgm_skb_get()</tt> increments the reference count of a PGM socket buffer.

<tt>pgm_free_skb()</tt> decrements the reference count a PGM socket buffer and if zero frees the underlying memory.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>size</tt></td>
<td>Size of the data segment.</td>
</tr>
</table>


### Example ###
Create a skbuff with enough space for a regular Ethernet sized packet, create a reference and free the original.
```
 struct pgm_sk_buff_t *skb, *ref_skb;
 skb = pgm_alloc_skb (1500);
 ref_skb = pgm_skb_get (skb);
 pgm_free_skb (skb);
```

### See Also ###
  * <tt>struct <a href='OpenPgm5CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><a href='OpenPgm5CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.
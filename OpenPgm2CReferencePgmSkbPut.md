_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer] *pgm_skb_put* (<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]*     skb,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]                   size<br>
);<br>
<br>
void *pgm_skb_reserve* (<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]*     skb,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]                   size<br>
);<br>
<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer] *pgm_skb_pull* (<br>
struct [OpenPgm2CReferencePgmSkBuffT pgm_sk_buff_t]*     skb,<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]                   size<br>
);<br>
</pre>

### Purpose ###
<tt>pgm_skb_put()</tt> puts data to the end of a skbuff.

<tt>pgm_skb_reserve()</tt> reserves space to add data in a skbuff.

<tt>pgm_skb_pull()</tt> pulls the data from the start of a skbuff.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>len</tt></td>
<td>Length in bytes.</td>
</tr>
</table>


### Return Value ###
<tt>pgm_skb_put()</tt> returns the current tail pointer of the skbuff before being updated and so can be used as a data pointer for copying.

### Example ###
Add data to a new PGM SKB.

```
 const char src[] = "i am not a string";
 struct pgm_sk_buff_t *skb = pgm_alloc_skb (1500);
 memcpy (pgm_skb_put (skb, sizeof(src)), src, sizeof(src));
```

Reserve space in a skbuff for a new header.

```
 struct app_header {
   int ah_seq;
 };
 struct pgm_sk_buff_t *skb = pgm_alloc_skb (1500);
 pgm_skb_reserve (skb, sizeof(struct app_header));
```

Pull the data pointer back in a skbuff after reading a header.

```
 struct app_header *ah = skb->data;
 g_message ("header sequence: %d", ah->ah_seq);
 pgm_skb_pull (skb, sizeof(struct app_header));
```

### See Also ###
  * <tt>struct <a href='OpenPgm2CReferencePgmSkBuffT.md'>pgm_sk_buff_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
</li><li><a href='OpenPgm2CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.</li></ul>

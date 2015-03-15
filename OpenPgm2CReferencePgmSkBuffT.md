#summary OpenPGM 2 : C Reference : PGM SKBs : struct pgm\_sk\_buff\_t
#labels Phase-Implementation
#sidebar TOC2CReferencePgmSkbs
_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
struct pgm_sk_buff_t {<br>
[http://library.gnome.org/devel/glib/stable/glib-Doubly-Linked-Lists.html#GList GList]                        link_;<br>
[OpenPgm2CReferencePgmTransportT pgm_transport_t]*             transport;<br>
[OpenPgm2CReferencePgmTimeT pgm_time_t]                   tstamp;<br>
[OpenPgm2CReferencePgmTsiT pgm_tsi_t]                    tsi;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint32 guint32]                      sequence;<br>
char                         cb[48];<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint16 guint16]                      len;<br>
unsigned                     zero_padded:1;<br>
struct pgm_header*           pgm_header;<br>
struct pgm_opt_fragment*     pgm_opt_fragment;<br>
struct pgm_data*             pgm_data;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]                     head;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]                     data;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]                     tail;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gpointer gpointer]                     end;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#guint guint]                        truesize;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gint gint]                         users;<br>
};<br>
</pre>

### Purpose ###
Object representing a PGM socket buffer.


### Remarks ###
Incoming SKBs are tagged after the call to <tt>recvmsg</tt> or <tt>WSARecvMsg</tt> completes, incurring an inaccuracy due to propagation delays and scheduling.

Whilst [kernel timestamping](http://lwn.net/Articles/325929/) mechanisms exist they are system specific.  Not all NICs support hardware timestamping, there are no guarantees that the NIC clock is monotonic, or even functions correctly at all and hence the extended API for software stamping fallback if the NIC failed.  The NIC clock need not be related to real time or core startup time, for instance it could be an offset to when the NIC processor started.  As the clock offset and resolution is likely to be incompatible to what is used for state timekeeping within OpenPGM this feature is not currently used.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmTimeT.md'>pgm_time_t</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTsiT.md'>pgm_tsi_t</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmAllocSkb.md'>pgm_free_skb()</a></tt><br>
</li><li><a href='OpenPgm2CReferencePgmSkbs.md'>PGM SKBs</a> in OpenPGM C Reference.
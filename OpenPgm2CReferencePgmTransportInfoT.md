_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct pgm_transport_info_t *pgm_transport_info_t*;<br>
<br>
struct pgm_transport_info_t {<br>
[OpenPgm2CReferencePgmGsiT pgm_gsi_t]                    ti_gsi;<br>
int                          ti_flags;<br>
int                          ti_family;<br>
int                          ti_udp_encap_ucast_port;<br>
int                          ti_udp_encap_mcast_port;<br>
int                          ti_sport;<br>
int                          ti_dport;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                        ti_recv_addrs_len;<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]*     ti_recv_addrs;<br>
[http://library.gnome.org/devel/glib/stable/glib-Basic-Types.html#gsize gsize]                        ti_send_addrs_len;<br>
struct [http://www.ietf.org/rfc/rfc3678.txt group_source_req]*     ti_send_addrs;<br>
};<br>
</pre>

### Purpose ###
A transport object represents network definition of a transport.

### See Also ###
  * <tt><a href='OpenPgm2CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm2CReferencePgmIfGetTransportInfo.md'>pgm_if_get_transport_info()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmIfGetTransportInfo.md'>pgm_if_free_transport_info()</a></tt><br>
</li><li><tt><a href='OpenPgm2CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
</li><li><a href='OpenPgm2CReferenceInterface.md'>Interface</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm2CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
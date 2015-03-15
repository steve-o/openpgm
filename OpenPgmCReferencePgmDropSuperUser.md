_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
void *pgm_drop_superuser* (void);<br>
</pre>

### Purpose ###
Drop superuser privileges needed to create PGM protocol sockets.

### Example ###
```
 pgm_gsi_t gsi;
 gsize recv_len = 1;
 struct group_source_req recv_gsr, send_gsr;
 pgm_transport_t* transport = NULL;
 pgm_create_md5_gsi (&gsi);
 pgm_if_parse_transport (";239.192.0.1", AF_UNSPEC, &recv_gsr, &recv_len, &send_gsr);
 pgm_transport_create (&transport, &gsi, 0, dport, &recv_gsr, recv_len, &send_gsr);
 pgm_drop_superuser ();
```

### See Also ###
  * <tt><a href='OpenPgmCReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt><br>
<ul><li><a href='OpenPgmCReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.</li></ul>

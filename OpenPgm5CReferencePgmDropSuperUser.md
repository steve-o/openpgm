#summary OpenPGM 5 : C Reference : Environment : pgm\_drop\_superuser()
#labels Phase-Implementation
#sidebar TOC5CReferenceEnvironment
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
void *pgm_drop_superuser* (void);<br>
</pre>

### Purpose ###
Drop superuser privileges needed to create PGM protocol sockets.

### Remarks ###
This function has no effect on Microsoft Windows platforms.

### Example ###
```
 pgm_sock_t* sock = NULL;
 pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_UDP, NULL);
 pgm_drop_superuser ();
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceEnvironment.md'>Environment</a> in OpenPGM C Reference.
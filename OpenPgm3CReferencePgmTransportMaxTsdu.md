#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_max\_tsdu()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
size_t *pgm_transport_max_tsdu* (<br>
[OpenPgm3CReferencePgmTransportT pgm_tranport_t]*     transport,<br>
bool*           can_fragment<br>
);<br>
</pre>

### Purpose ###
Get maximum TSDU, or packet payload size with or without fragmentation.

### Remarks ###
The underlying transport handles packets sized to the maximum transmission protocol data unit (TPDU), when an application provides a larger APDU it will fragment the message into many packets which the receivers will reconstruct and present as a single data unit.  The maximum size available per packet for application payload varies depending whether it is part of a larger APDU.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>PGM Transport.</td>
</tr><tr>
<td><tt>can_fragment</tt></td>
<td>Fragmented or non-fragmented packet.</td>
</tr>
</table>


### Return Value ###
Returns the maximum TSDU size, or number of bytes available per packet as application payload.

### Example ###
Send one zero-copy packet sized APDU.

```
 const size_t max_tpdu = 1500;
 struct pgm_sk_buff_t vector[1];
 vector[0] = pgm_alloc_skb (max_tpdu);
 pgm_send_skbv (transport, vector, 1, true, NULL);
```

Send a 10 KB zero-copy sized APDU.

```
 const size_t max_tpdu = 1500;
 size_t apdu_size = 10 * 1024;
 size_t tsdu_size = pgm_transport_max_tsdu (transport, true);
 unsigned packets = apdu_size / tsdu_size;
 struct pgm_sk_buff_t vector[packets];
 for (unsigned i = 0; i < packets; i++)
   vector[i] = pgm_alloc_skb (max_tpdu);
 pgm_send_skbv (transport, vector, packets, true, NULL);
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmAllocSkb.md'>pgm_alloc_skb()</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmSendSkbv.md'>pgm_send_skbv()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
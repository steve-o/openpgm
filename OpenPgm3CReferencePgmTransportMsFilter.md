#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_msfilter()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_msfilter* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]*         transport,<br>
struct group_filter*     gf_list,<br>
size_t                    len<br>
);<br>
</pre>

### Purpose ###
Block or re-allow packets from a list of source IP addresses.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>transport</tt></td>
<td>The PGM transport object.</td>
</tr><tr>
<td><tt>gf_list</tt></td>
<td>List of source/group pairs with optional interfaces.</td>
</tr><tr>
<td><tt>len</tt></td>
<td>Length of <tt>gf_list</tt> in bytes.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On error, <tt>false</tt> is returned.

### Example ###
Exclude sources 192.168.9.2/3 from group 239.192.0.2.

```
 struct group_filter* gf_list = calloc (1, GROUP_FILTER_SIZE(2));
 ((struct sockaddr*)&gf_list->gf_group)->sa_family = AF_INET;
 ((struct sockaddr_in*)&gf_list->gf_group)->sin_addr.s_addr = inet_addr("239.192.0.2");
 gf_list->gf_fmode = MCAST_EXCLUDE;
 gf_list->gf_numsrc = 2;
 ((struct sockaddr*)&gf_list->gf_slist[0])->sa_family = AF_INET;
 ((struct sockaddr_in*)&gf_list->gf_slist[0])->sin_addr.s_addr = inet_addr("192.168.9.2");
 ((struct sockaddr*)&gf_list->gf_slist[1])->sa_family = AF_INET;
 ((struct sockaddr_in*)&gf_list->gf_slist[1])->sin_addr.s_addr = inet_addr("192.168.9.3");
 pgm_transport_msfilter (transport, gf_list, GROUP_FILTER_SIZE(2));
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmTransportBlockSource.md'>pgm_transport_block_source()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportJoinGroup.md'>pgm_transport_join_group()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportJoinSourceGroup.md'>pgm_transport_join_source_group()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.
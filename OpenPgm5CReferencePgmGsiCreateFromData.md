#summary OpenPGM 5 C Reference : Socket : pgm\_gsi\_create\_from\_data()
#labels Phase-Implementation
#sidebar TOC5CReferenceSocket
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_gsi_create_from_data* (<br>
[OpenPgm5CReferencePgmGsiT pgm_gsi_t]*           gsi,<br>
const void*          buf,<br>
size_t               length<br>
);<br>
</pre>

### Purpose ###
Create a GSI based on MD5 of provided data buffer.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>gsi</tt></td>
<td>A pointer to the GSI provided by the application that the function will fill.</td>
</tr><tr>
<td><tt>buf</tt></td>
<td>Data buffer.</td>
</tr><tr>
<td><tt>length</tt></td>
<td>Length of <tt>buf</tt>.</td>
</tr>
</table>


### Return Value ###
On success, <tt>true</tt> is returned.  On invalid parameters, <tt>false</tt> is returned.

### Example ###
```
 pgm_gsi_t gsi;
 unsigned char buf[] = { 0x1, 0x2, 0x3, 0x4 };
 if (!pgm_create_data_gsi (&gsi, buf, sizeof(buf))) {
   fprintf (stderr, "Invalid parameters.\n");
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmGsiT.md'>pgm_gsi_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromAddr.md'>pgm_gsi_create_from_addr()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmGsiCreateFromHostname.md'>pgm_gsi_create_from_hostname()</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceSocket.md'>Socket</a> in OpenPGM C Reference.
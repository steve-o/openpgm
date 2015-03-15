_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_getaddrinfo* (<br>
const char*                  restrict network,<br>
const struct [OpenPgm5CReferencePgmAddrInfoT pgm_addrinfo_t]* restrict hints,<br>
struct [OpenPgm5CReferencePgmAddrInfoT pgm_addrinfo_t]**      restrict res,<br>
[OpenPgm5CReferencePgmErrorT pgm_error_t]**                restrict error<br>
);<br>
<br>
void *pgm_freeaddrinfo* (<br>
struct [OpenPgm5CReferencePgmAddrInfoT pgm_addrinfo_t]*   res<br>
);<br>
</pre>

### Purpose ###
Decompose a string network specification.

### Remarks ###
The TIBCO Rendezvous network parameter provides a convenient compact representation of the complicated data structures needed to specify sending and receiving interfaces and multicast addresses.  This function provides conversion between the network string and the <tt>struct group_req</tt> parameters of <tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_setsockopt()</a></tt>.

The returned <tt><a href='OpenPgm5CReferencePgmAddrInfoT.md'>pgm_addrinfo_t</a></tt> must be freed with <tt>pgm_freeaddrinfo()</tt>.

<tt>pgm_getaddrinfo</tt> is analogous to <tt>getaddrinfo</tt> with <tt>AI_PASSIVE</tt> set, it will only return resolved interfaces for the host that it is executed upon.  Consider that network specifications are common to include network or interface names that are not globally well defined.

### Parameters ###

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Parameter</th>
<th>Description</th>
</tr>
<tr>
<td><tt>network</tt></td>
<td>A TIBCO Rendezvous compatible network parameter string</td>
</tr><tr>
<td><tt>hints</tt></td>
<td>Transport info structure whose <tt>ai_family</tt> specify criteria that limit the set of addresses returned by <tt>pgm_getaddrinfo()</tt>.</td>
</tr><tr>
<td><tt>res</tt></td>
<td>return location for resolved transport info structure.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>return location for error.</td>
</tr>
</table>


### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.


### Errors ###

**PGM\_ERROR\_DOMAIN\_IF, PGM\_ERROR\_INVAL,**
  * _"'%c' is not a valid character."_
    * Valid hostnames must conform to RFC 952, i.e. contain '`a`'-'`z`', `0`-`9`, a hyphen '`-`' or period '`.`'.  Valid IPv4 addresses can contain `0`-`9`, a period '`.`' and a forward slash '`/`' for CIDR blocks.  Valid IPv6 addresses can in addition contain a colon '`:`' and square brackets '`[`', '`]`'.  IPv6 addressing with zones may in addition contain percent mark '`%`' and underscore '`_`'.
  * _"Send group list contains more than one entity."_
    * PGM cannot simultaneously send to more than one group.
  * _"Unresolvable receive group %s%s%s"_

**PGM\_ERROR\_DOMAIN\_IF, PGM\_ERROR\_XDEV,**
  * _"Expecting network interface address, found IPv4 multicast network %s%s%s"_
  * _"Expecting network interface address, found IPv6 multicast network %s%s%s"_
  * _"Expecting interface address, found IPv4 multicast address %s%s%s"_
  * _"Expecting interface address, found IPv6 multicast address %s%s%s"_
  * _"Expecting interface address, found IPv4 multicast name %s%s%s"_
  * _"Expecting interface address, found IPv6 multicast name %s%s%s"_
  * _"Network name %s%s%s resolves to IPv4 mulicast address."_
  * _"Network name %s%s%s resolves to IPv6 mulicast address."_

**PGM\_ERROR\_DOMAIN\_IF, PGM\_ERROR\_NODEV,**
  * _"IP address family conflict when resolving network name %s%s%s, found AF\_INET when AF\_INET6 expected."_
  * _"IP address family conflict when resolving network name %s%s%s, found AF\_INET6 when AF\_INET expected."_
  * _"IP address family conflict when resolving network name %s%s%s, found IPv4 when IPv6 expected."_
  * _"IP address family conflict when resolving network name %s%s%s, found IPv6 when IPv4 expected."_
  * _"Not configured for IPv6 network name support, %s%s%s is an IPv6 network name."_
  * _"IP address class conflict when resolving network name %s%s%s, expected IPv4 multicast."_
  * _"IP address class conflict when resolving network name %s%s%s, expected IPv6 multicast."_
  * _"Network name resolves to non-internet protocol address family %s%s%s"_
  * _"No matching non-loopback and multicast capable network interface %s%s%s"_

**PGM\_ERROR\_DOMAIN\_IF, PGM\_ERROR\_NOTUNIQ,**
  * _"Multiple interfaces found with network address %s."_
  * _"Network interface name not unique %s%s%s"_
    * PGM cannot simultaneously send to more than one group.

**PGM\_ERROR\_DOMAIN\_IF,**
  * _"Numeric host resolution: %s(%d)"_
  * _"Internet host resolution: %s(%d)"_
  * _"Resolving receive group: %s(%d)"_
    * Platform specific error.


### Example ###
Basic example using default hints.
```
 char* network = "eth0;226.0.0.1";
 struct pgm_addrinfo_t* res = NULL;
 pgm_error_t* err = NULL;
 
 if (!pgm_getaddrinfo (network, NULL, &res, &err)) {
   fprintf (stderr, "Parsing network parameter: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmErrorT.md'>pgm_error_t</a></tt><br>
<ul><li><tt><a href='OpenPgm5CReferencePgmAddrInfoT.md'>pgm_addrinfo_t</a></tt><br>
</li><li><tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_setsockopt()</a></tt><br>
</li><li><a href='OpenPgm5CReferenceInterface.md'>Interface</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.<br>
</li><li><a href='OpenPgmConceptsTransport#Network_Parameters.md'>Network Parameters</a> in the article Transport in OpenPGM Concepts.
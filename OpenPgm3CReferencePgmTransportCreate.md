#summary OpenPGM 3 : C Reference : Transport : pgm\_transport\_create()
#labels Phase-Implementation
#sidebar TOC3CReferenceTransport
_Function_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
bool *pgm_transport_create* (<br>
[OpenPgm3CReferencePgmTransportT pgm_transport_t]* const           transport,<br>
struct [OpenPgm3CReferencePgmTransportInfoT pgm_transport_info_t]*     tinfo,<br>
[OpenPgm3CReferencePgmErrorT pgm_error_t]**                         error<br>
);<br>
</pre>

### Purpose ###
Create a network transport.

### Remarks ###
A custom network protocol requires super-user privileges to open the necessary raw sockets.  An application can call <tt>pgm_transport_create()</tt> to create unbound raw sockets, drop the privileges to maintain security, and finally bind with <tt><a href='OpenPgm3CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt> to start processing incoming packets.

A PGM data-source port is a unique inbox per GSI different from the data-destination port.  When an application restarts it should use a new data-source port to distinguish its sequence number usage from previous instances.  Special applications such as file carousels may wish to re-join previous sessions if sequence numbers are mapped to file contents.  Specifying a value of 0 for a PGM data-source port will randomly generate a unique value.  Administrators running multiple PGM applications on the same host using multicast loop should pay attention to the possibility that serious communication problems will occur if applications pick the same data-source port.

Both <tt>pgm_transport_info_t::ti_recv_addrs</tt> and <tt>pgm_transport_info_t::ti_send_addrs</tt> are populated as per RFC 3678.  <tt><a href='OpenPgm3CReferencePgmIfGetTransportInfo.md'>pgm_if_get_transport_info()</a></tt> provides a convenient method of defining the network parameters with a TIBCO Rendezvous network parameter compatible string.  By default an any-source multicast configuration is enabled, set <tt>group_source_req::gsr_source</tt> to the IP address of a sending computer for source-specific multicast (SSM).  Set <tt>pgm_transport_info_t::ti_udp_encap_ucast_port</tt> and <tt>pgm_transport_info_t::ti_encap_mcast_port</tt> entities to specify the ports to use for UDP encapsulation.

SSM destination addresses must be in the ranges 232/8 for IPv4 or FF3 _x_::/96 for IPv6.

Try to avoid 224.0.0._x_ addresses as many of these [multicast addresses](http://en.wikipedia.org/wiki/Multicast_address) are used by network infrastructure equipment and have special purposes.  The recommendation on private networks is to used the administratively scoped range 239/8 (239.192/16 for local-scope) as per RFC 2365 and RFC 3171.  Public Internet usage of multicast should be careful of conflicts with [IANA assigned multicast addresses](http://www.iana.org/assignments/multicast-addresses) and should consider 233/8 [GLOP addressing](http://en.wikipedia.org/wiki/GLOP) as per RFC 3180.

IP Multicasting uses Layer 2 MAC addressing to transmit a packet to a group of hosts on a LAN segment.  The Layer 3 address (224.0.0.0 to 239.255.255.255) are mapped to Layer 2 addresses (0x0100.5E _xx_._xxxx_).  Because all 28 bits of the Layer 3 IP multicast address information cannot be mapped into the available 23 bits of MAC address space, five bits of address information are lost in the mapping process.  This means that each IP multicast MAC address can represent 32 IP multicast addresses.

IGMP Snooping normally is used by Layer 2 switches to constrain multicast traffic only to those ports that have hosts attached and that have signalled their desire to join the multicast group by sending IGMP Membership Reports.  IGMP Snooping is disabled for the special 224.0.0.1 - <tt>ALL-HOSTS</tt> group, 224.0.0.2 - <tt>ALL-ROUTERS</tt> group, 224.0.0.5 - <tt>ALL-OSPF-ROUTERS</tt> group, etc.  This means any IP multicast group matching 224-239.0.0/24 and 224-239.128.0/24 will also cause Layer 2 flooding and should be avoided.

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
<td><tt>tinfo</tt></td>
<td>Transport information object.</td>
</tr><tr>
<td><tt>error</tt></td>
<td>a return location for a <tt><a href='OpenPgm3CReferencePgmErrorT.md'>pgm_error_t</a></tt>, or <tt>NULL</tt>.</td>
</tr>
</table>

### Return Value ###
On success, <tt>true</tt> is returned.  On failure, <tt>false</tt> is returned and if <tt>error</tt> is set it may be populated with details of the exception.

### Example ###
Regular PGM protocol example using the <tt>eth0</tt> interface (popular Linux adapter name), the multicast address 239.192.0.1, and the data-destination port 7500.

```
 pgm_transport_t* transport = NULL;
 struct pgm_transport_info_t* res = NULL;
 pgm_error_t* err = NULL;
 if (!pgm_if_get_transport_info ("", NULL, &res, &err)) {
   fprintf ("parsing network parameter: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
 res->ti_dport = 7500;
 if (!pgm_transport_create (&transport, res, &err)) {
   fprintf ("creating transport: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

A UDP encapsulated example using UDP ports 3055 for unicast and 3056 for multicast.

```
 pgm_transport_t* transport = NULL;
 struct pgm_transport_info_t* res = NULL;
 pgm_error_t* err = NULL;
 if (!pgm_if_get_transport_info ("", NULL, &res, &err)) {
   fprintf ("parsing network parameter: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
 res->ti_dport = 7500;
 res->ti_udp_encap_ucast_port = DEFAULT_UDP_ENCAP_UCAST_PORT;
 res->ti_udp_encap_mcast_port = DEFAULT_UDP_ENCAP_MCAST_PORT;
 if (!pgm_transport_create (&transport, res, &err)) {
   fprintf ("creating transport: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

A source-specific multicast session, subscribing to group 232.0.1.1 from host 172.16.0.1.

```
 pgm_transport_t* transport = NULL;
 struct pgm_transport_info_t* res = NULL;
 GError* err = NULL;
 if (!pgm_if_get_transport_info (";232.0.1.1", NULL, &res, &err)) {
   fprintf ("parsing network parameter: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
 res->ti_dport = 7500;
 res->ti_udp_encap_ucast_port = DEFAULT_UDP_ENCAP_UCAST_PORT;
 res->ti_udp_encap_mcast_port = DEFAULT_UDP_ENCAP_MCAST_PORT;
 ((struct sockaddr_in*)&res->ti_recv_addrs[0].gsr_source)->sin_addr.s_addr = inet_addr("176.16.0.1");
 if (!pgm_transport_create (&transport, res, &err)) {
   fprintf ("creating transport: %s", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

### See Also ###
  * <tt><a href='OpenPgm3CReferencePgmTransportT.md'>pgm_transport_t</a></tt><br>
<ul><li><tt><a href='OpenPgm3CReferencePgmIfGetTransportInfo.md'>pgm_if_get_transport_info()</a></tt><br>
</li><li><tt><a href='OpenPgm3CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt><br>
</li><li><a href='OpenPgm3CReferenceTransport.md'>Transport</a> in OpenPGM C Reference.<br>
</li><li><tt>struct <a href='http://www.ietf.org/rfc/rfc3678.txt'>group_source_req</a></tt> in RFC 3678.<br>
</li><li><a href='http://www.iana.org/assignments/multicast-addresses'>Assigned multicast addresses</a> at <a href='http://en.wikipedia.org/wiki/Internet_Assigned_Numbers_Authority'>IANA</a>.<br>
</li><li><a href='http://www.cisco.com/en/US/tech/tk828/technologies_white_paper09186a00802d4643.shtml'>Guidelines for Enterprise IP Multicast Address Allocation</a> at Cisco.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Multicast_address'>Multicast address</a> in Wikipedia.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Any-source_multicast'>Any-source multicast (ASM)</a> in Wikipedia.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Source-specific_multicast'>Source-source multicast (SSM)</a> in Wikipedia.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Internet_Group_Management_Protocol'>Internet Group Management Protocol (IGMP)</a> in Wikipedia.<br>
</li><li><a href='http://en.wikipedia.org/wiki/Multicast_Listener_Discovery'>Multicast Listener Discovery (MLD)</a> in Wikipedia.
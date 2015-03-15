#summary OpenPGM 5 : C Reference : Migration
#labels Phase-Implementation
#sidebar TOC5CReferenceMigration
## Interface and addressing ##
### Summary ###
  * Replace <tt>struct <a href='OpenPgm3CReferencePgmTransportInfoT.md'>pgm_transport_info_t</a></tt> with <tt>struct <a href='OpenPgm5CReferencePgmAddrInfoT.md'>pgm_addrinfo_t</a></tt>.
  * Replace interface parsing calls to <tt><a href='OpenPgm3CReferencePgmIfGetTransportInfo.md'>pgm_if_get_transport_info()</a></tt> with <tt><a href='OpenPgm5CReferencePgmGetAddrInfo.md'>pgm_getaddrinfo()</a></tt>.
  * Replace calls to <tt><a href='OpenPgm3CReferencePgmIfGetTransportInfo.md'>pgm_if_free_transport_info()</a></tt> with <tt><a href='OpenPgm5CReferencePgmGetAddrInfo.md'>pgm_freeaddrinfo()</a></tt>.


### Details ###
Version 5.0 follows the BSD socket interface for all basic interactions with OpenPGM, calls to <tt><a href='OpenPgm5CReferencePgmGetAddrInfo.md'>pgm_getaddrinfo()</a></tt> are equivalent to <tt>getaddrinfo()</tt>.

There is no implemented equivalent to <tt>getnameinfo()</tt>.


**Example version 3.0 code using UDP encapsulation and hostname based GSI.**
```
 struct pgm_transport_info_t* res = NULL;
 pgm_error_t* err = NULL;
 
 if (!pgm_if_get_transport_info (network, NULL, &res, &err)) {
   fprintf ("Parsing network parameter: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
 if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &err))) {
   fprintf ("Creating GSI: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_if_free_transport_info (res);
   return EXIT_FAILURE;
 }
 /* populate UDP encapsulation port */
 res->ti_udp_encap_ucast_port = udp_encap_port;
 res->ti_udp_encap_mcast_port = udp_encap_port;
```
**Example updated for version 5.**
```
 struct pgm_addrinfo_t* res = NULL;
 pgm_error_t* err = NULL;
 
 if (!pgm_getaddrinfo (network, NULL, &res, &err)) {
   fprintf ("Parsing network parameter: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   return EXIT_FAILURE;
 }
```

## Transport ##
### Summary ###
  * Transport is renamed to socket.
  * All transport setters and getters are merged into two APIs, <tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_setsockopt()</a></tt> and <tt><a href='OpenPgm5CReferencePgmGetSockOpt.md'>pgm_getsockopt()</a></tt>.
  * <tt><a href='OpenPgm3CReferencePgmTransportCreate.md'>pgm_transport_create()</a></tt> renames to <tt><a href='OpenPgm5CReferencePgmSocket.md'>pgm_socket()</a></tt> and sets the protocol and family.
  * <tt><a href='OpenPgm3CReferencePgmTransportBind.md'>pgm_transport_bind()</a></tt> renames to <tt><a href='OpenPgm5CReferencePgmBind.md'>pgm_bind()</a></tt> and takes the GSI and send and receive interfaces.
  * New API <tt><a href='OpenPgm5CReferencePgmConnect.md'>pgm_connect()</a></tt> to start the PGM protocol on the socket.
  * <tt><a href='OpenPgm3CReferencePgmTransportDestroy.md'>pgm_transport_destroy()</a></tt> renames to <tt><a href='OpenPgm5CReferencePgmClose.md'>pgm_close()</a></tt>.


### Details ###
Version 5.0 follows BSD socket semantics.

**Example version 3.0 code.**
```
 pgm_transport_t *transport;
 struct pgm_transport_info_t* tinfo;
 char buffer[4096];
 size_t bytes_read, bytes_sent;
 int status;
 pgm_error_t* err = NULL;
 
 if (!pgm_transport_create (&transport, tinfo, &err)) {
   fprintf (stderr, "Creating transport: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_if_free_transport_info (tinfo);
   return EXIT_FAILURE;
 }
 pgm_if_free_transport_info (tinfo);
 pgm_transport_set_max_tpdu (transport, max_tpdu);
 pgm_transport_set_txw_sqns (transport, sqns);
 pgm_transport_set_txw_max_rte (transport, max_rte);
 pgm_transport_set_multicast_loop (transport, multicast_loop);
 pgm_transport_set_hops (transport, hops);
 pgm_transport_set_ambient_spm (transport, spm_ambient);
 pgm_transport_set_heartbeat_spm (transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
 if (!pgm_transport_bind (transport, &err)) {
   fprintf (stderr, "binding transport: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_transport_destroy (transport, FALSE);
   return EXIT_FAILURE;
 }
 status = pgm_recv (transport, buffer, sizeof(buffer), 0, &bytes_read, &err);
 if (PGM_IO_STATUS_NORMAL == status)
   printf ("recv: %s\n", buffer);
 status = pgm_send (transport, buffer, bytes_read, &bytes_sent);
 if (PGM_IO_STATUS_NORMAL == status)
   printf ("sent: %d bytes\n", bytes_sent);
```

**Updated code for version 5.0.**

```
 pgm_sock_t *sock;
 struct pgm_sockaddr_t addr;
 struct group_req send_addr, recv_addr;
 char buffer[4096];
 size_t bytes_read, bytes_sent;
 int status;
 pgm_error_t* err = NULL;

 if (!pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_UDP, &err)) {
   fprintf (stderr, "Creating socket: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_freeaddrinfo (res);
   return EXIT_FAILURE;
 }
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_MTU, &max_tpdu, sizeof(max_tpdu));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_TXW_SQNS, &sqns, sizeof(sqns));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_TXW_MAX_RTE, &max_rte, sizeof(max_rte));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_AMBIENT_SPM, &ambient_spm, sizeof(ambient_spm));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_HEARTBEAT_SPM, &heartbeat_spm, sizeof(heartbeat_spm));
 if (!pgm_bind (sock, &addr, sizeof(addr), &err)) {
   fprintf (stderr, "Binding socket: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_close (sock, FALSE);
   return EXIT_FAILURE;
 }
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_JOIN_GROUP, &recv_addr, sizeof(recv_addr));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_SEND_GROUP, &send_addr, sizeof(send_addr));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
 pgm_setsockopt (sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
 if (!pgm_connect (sock, &pgm_err)) {
   fprintf (stderr, "Connecting socket: %s\n", (err && err->message) ? err->message : "(null)");
   pgm_error_free (err);
   pgm_close (sock, FALSE);
   return EXIT_FAILURE;
 }
 status = pgm_recv (sock, buffer, sizeof(buffer), 0, &bytes_read, &err);
 if (PGM_IO_STATUS_NORMAL == status)
   printf ("recv: %s\n", buffer);
 status = pgm_send (sock, buffer, bytes_read, &bytes_sent);
 if (PGM_IO_STATUS_NORMAL == status)
   printf ("sent: %d bytes\n", bytes_sent);
```

#include <stdlib.h>
#include <check.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "check_pgm.h"

#include "pgm/if.h"


#define IF_DEFAULT_INTERFACE	"eth0.615"
#define IF_DEFAULT_ADDRESS	"10.6.15.69"
#define IF_DEFAULT_GROUP	((in_addr_t) 0xefc00001) /* 239.192.0.1 */

#define IF6_DEFAULT_INIT { { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }	/* ff08::1 */
static const struct in6_addr if6_default_group_addr = IF6_DEFAULT_INIT;

#define IF6_DEFAULT_ADDRESS_INIT { { { 0xfe,0x80,0,0,0,0,0,0,2,0x30,0x1b,0xff,0xfe,0xb7,0xa2,9 } } }
	/* "fe80::230:1bff:feb7:a209/64" */
static const struct in6_addr if6_default_interface_addr = IF6_DEFAULT_ADDRESS_INIT;


static int	g_ai_family = 0;


static void
setup_unspec (void)
{
	g_ai_family = AF_UNSPEC;
}

static void
setup_ipv4 (void)
{
	g_ai_family = AF_INET;
}

static void
setup_ipv6 (void)
{
	g_ai_family = AF_INET6;
}

static void
teardown (void)
{
}

/* return 0 if gsr multicast group does not match the default PGM group, otherwise return -1 */
static int
match_default_group (
	int				ai_family,
	struct group_source_req*	gsr
	)
{
	const struct sockaddr_in if_default_group = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = g_htonl(IF_DEFAULT_GROUP)
	};

	const struct sockaddr_in6 if6_default_group = {
		.sin6_family = AF_INET6,
		.sin6_addr = if6_default_group_addr
	};

	switch (ai_family) {
	case AF_UNSPEC:
	case AF_INET:
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&if_default_group);

	case AF_INET6:
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&if6_default_group);

	default:
		return 0;
	}
}

/* return 0 if gsr source inteface does not match the INADDR_ANY reserved address, otherwise return -1 */
static int
match_any_source (
	int				ai_family,
	struct group_source_req*	gsr
	)
{
	const struct sockaddr_in if_any_source = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY
	};

	const struct sockaddr_in6 if6_any_source = {
		.sin6_family = AF_INET6,
		.sin6_addr = in6addr_any
	};

	switch (ai_family) {
	case AF_UNSPEC:
	case AF_INET:
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (const struct sockaddr*)&if_any_source);

	case AF_INET6:
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (const struct sockaddr*)&if6_any_source);

	default:
		return 0;
	}
}

/* return 0 if gsr source interface does not match the IF_DEFAULT_INTERFACE, otherwise return -1 */
static int
match_default_interface (
	int				ai_family,
	struct group_source_req*	gsr
	)
{
	const struct sockaddr_in if_default_interface = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = inet_addr(IF_DEFAULT_ADDRESS)
	};

	const struct sockaddr_in6 if6_default_interface = {
		.sin6_family = AF_INET6,
		.sin6_addr = if6_default_interface_addr
	};

	switch (ai_family) {
	case AF_UNSPEC:
	case AF_INET:
/*
 *
{
char t[INET6_ADDRSTRLEN];
pgm_sockaddr_ntop (&gsr->gsr_source, t, sizeof(t));
g_message ("source %s", t);
pgm_sockaddr_ntop (&if_default_interface, t, sizeof(t));
g_message ("default interface %s", t);
}
 *
 */
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (const struct sockaddr*)&if_default_interface);

	case AF_INET6:
/*
 *
{
char t[INET6_ADDRSTRLEN];
pgm_sockaddr_ntop (&gsr->gsr_source, t, sizeof(t));
g_message ("source %s", t);
pgm_sockaddr_ntop (&if6_default_interface, t, sizeof(t));
g_message ("default interface %s", t);
}
 *
 */
		return 0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (const struct sockaddr*)&if6_default_interface);

	default:
		return 0;
	}
}



/* target: pgm_if_parse_transport (
 * 			const char*			s,
 * 			int				ai_family,
 * 			struct group_source_req*	recv_gsr,
 * 			int*				recv_len,
 * 			struct group_source_req*	send_gsr
 * 			)
 *
 * test empty string
 *
 * pre-condition: ai_family ∈ { AF_UNSPEC, AF_INET, AF_INET6 }
 */
START_TEST (test_parse_transport_000)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= "";
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr;
	int			recv_len	= 1;
	struct group_source_req send_gsr;

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, &recv_gsr, &recv_len, &send_gsr));
	fail_unless (1 == recv_len);

	fail_unless (match_default_group (ai_family, &recv_gsr));
	fail_unless (match_any_source (ai_family, &recv_gsr));
	fail_unless (match_default_group (ai_family, &send_gsr));
	fail_unless (match_any_source (ai_family, &send_gsr));
}
END_TEST

/* interface name
 *
 * pre-condition: interface defined to match running host
 */
START_TEST (test_parse_transport_001)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= IF_DEFAULT_INTERFACE;
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	int			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (G_N_ELEMENTS(recv_gsr) == recv_len);

	fail_unless (match_default_group (ai_family, recv_gsr));
	fail_unless (match_default_interface (ai_family, recv_gsr));
	fail_unless (match_default_group (ai_family, send_gsr));
	fail_unless (match_default_interface (ai_family, send_gsr));
}
END_TEST

/* interface plus semi-colon, e.g. "eth0;"
 */

START_TEST (test_parse_transport_002)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= IF_DEFAULT_INTERFACE ";";
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	int			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (G_N_ELEMENTS(recv_gsr) == recv_len);

	fail_unless (match_default_group (ai_family, recv_gsr));
	fail_unless (match_default_interface (ai_family, recv_gsr));
	fail_unless (match_default_group (ai_family, send_gsr));
	fail_unless (match_default_interface (ai_family, send_gsr));
}
END_TEST

/* interface plus two semi-colons, e.g. "eth0;;"
 */

START_TEST (test_parse_transport_003)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= IF_DEFAULT_INTERFACE ";;";
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	int			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (G_N_ELEMENTS(recv_gsr) == recv_len);

	fail_unless (match_default_group (ai_family, recv_gsr));
	fail_unless (match_default_interface (ai_family, recv_gsr));
	fail_unless (match_default_group (ai_family, send_gsr));
	fail_unless (match_default_interface (ai_family, send_gsr));
}
END_TEST



/* too many semi-colons
 */
START_TEST (test_parse_transport_fail_000)
{
	const char*		s		= ";;;";
	int			ai_family	= AF_UNSPEC;
	struct group_source_req	recv_gsr[1];
	int			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST



Suite*
make_if_suite (void)
{
	Suite* s = suite_create ("pgm_if*()");

	TCase* tc_parse_transport_unspec = tcase_create ("parse_transport_unspec");
	TCase* tc_parse_transport_ipv4 = tcase_create ("parse_transport_ipv4");
	TCase* tc_parse_transport_ipv6 = tcase_create ("parse_transport_ipv6");

	tcase_add_checked_fixture (tc_parse_transport_unspec, setup_unspec, teardown);
	tcase_add_checked_fixture (tc_parse_transport_ipv4, setup_ipv4, teardown);
	tcase_add_checked_fixture (tc_parse_transport_ipv6, setup_ipv6, teardown);

/* unspecified address family, ai_family == AF_UNSPEC */
	suite_add_tcase (s, tc_parse_transport_unspec);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_000);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_001);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_002);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_003);
//	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_000);

/* forced IPv4 */
	suite_add_tcase (s, tc_parse_transport_ipv4);
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_000);
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_001);
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_002);
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_003);

/* forced IPv6 */
	suite_add_tcase (s, tc_parse_transport_ipv6);
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_000);
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_001);
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_002);
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_003);


	return s;
}

/* eof */

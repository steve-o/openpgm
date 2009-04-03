
#include <stdlib.h>
#include <check.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "check_pgm.h"

#include "pgm/if.h"
#include "pgm/sockaddr.h"

/* parameters to hardcode to the running host
 */

#define IF_HOSTNAME		"ayaka"
#define IF6_HOSTNAME		"ip6-ayaka"		/* ping6 doesn't work on fe80:: */
#define IF_NETWORK		"private"		/* /etc/networks */
#define IF6_NETWORK		"ip6-private"
#define IF_DEFAULT_INTERFACE	"eth0"
#define IF_DEFAULT_ADDRESS	"10.6.28.31"
#define IF_DEFAULT_GROUP	((in_addr_t) 0xefc00001) /* 239.192.0.1 */

#define IF6_DEFAULT_INIT { { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }	/* ff08::1 */
static const struct in6_addr if6_default_group_addr = IF6_DEFAULT_INIT;

#define IF6_DEFAULT_ADDRESS	"2002:dce8:d28e::31"
#define IF6_DEFAULT_ADDRESS_INIT { { { 0x20,2,0xdc,0xe8,0xd2,0x8e,0,0,0,0,0,0,0,0,0,0x31 } } }
static const struct in6_addr if6_default_interface_addr = IF6_DEFAULT_ADDRESS_INIT;


static int	g_ai_family = 0;


static void
setup_unspec (void)
{
//	puts ("setup unspecified address family.");
	g_ai_family = AF_UNSPEC;
}

static void
setup_ipv4 (void)
{
//	puts ("setup IPv4 forced address family.");
	g_ai_family = AF_INET;
}

static void
setup_ipv6 (void)
{
//	puts ("setup IPv6 forced address family.");
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
#if 0
		return ai_family == pgm_sockaddr_family(&gsr->gsr_source);
#endif

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
 * test network parameters that resolve to default group and any source (interface).
 *
 * pre-condition: ai_family ∈ { AF_UNSPEC, AF_INET, AF_INET6 }
 */

struct if_test_case_t {
	const char* ipv4;
	const char* ipv6;
};

typedef struct if_test_case_t if_test_case_t;

#define IPV4_AND_IPV6(x)	x, x

static const if_test_case_t cases_000[] = {
	{ 	IPV4_AND_IPV6("")	},
	{ 	IPV4_AND_IPV6(";")	},
	{ 	IPV4_AND_IPV6(";;")	},
	{ "239.192.0.1",			"ff08::1"				},
	{ ";239.192.0.1",			";ff08::1"				},
	{ ";239.192.0.1;239.192.0.1",		";ff08::1;ff08::1"			},
	{ "PGM.MCAST.NET",			"IP6-PGM.MCAST.NET"			},
	{ ";PGM.MCAST.NET",			";IP6-PGM.MCAST.NET"			},
	{ ";PGM.MCAST.NET;PGM.MCAST.NET",	";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ ";239.192.0.1;PGM.MCAST.NET",		";ff08::1;IP6-PGM.MCAST.NET"		},
	{ ";PGM.MCAST.NET;239.192.0.1",		";IP6-PGM.MCAST.NET;ff08::1"		}
};

START_TEST (test_parse_transport_000)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= (g_ai_family == AF_INET6) ? cases_000[_i].ipv6 : cases_000[_i].ipv4;
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr;
	gsize			recv_len	= 1;
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
 * 		  ipv4 and ipv6 hostnames are different, otherwise "<hostname>" tests might go unexpected.
 */

static const if_test_case_t cases_001[] = {
	{ IF_DEFAULT_INTERFACE,					/* † */ IF_DEFAULT_INTERFACE		},
	{ IF_DEFAULT_INTERFACE ";",				/* † */ IF_DEFAULT_INTERFACE ";"		},
	{ IF_DEFAULT_INTERFACE ";;",				/* † */ IF_DEFAULT_INTERFACE ";;"	},
	{ IF_DEFAULT_INTERFACE ";239.192.0.1",			/* † */ IF_DEFAULT_INTERFACE ";ff08::1"			},
	{ IF_DEFAULT_INTERFACE ";239.192.0.1;239.192.0.1",	/* † */ IF_DEFAULT_INTERFACE ";ff08::1;ff08::1"		},
	{ IF_DEFAULT_INTERFACE ";PGM.MCAST.NET",		/* † */ IF_DEFAULT_INTERFACE ";IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_INTERFACE ";PGM.MCAST.NET;PGM.MCAST.NET",	/* † */ IF_DEFAULT_INTERFACE ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_INTERFACE ";239.192.0.1;PGM.MCAST.NET",	/* † */ IF_DEFAULT_INTERFACE ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_INTERFACE ";PGM.MCAST.NET;239.192.0.1",	/* † */	IF_DEFAULT_INTERFACE ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ IF_DEFAULT_ADDRESS,					IF6_DEFAULT_ADDRESS			},
	{ IF_DEFAULT_ADDRESS ";",				IF6_DEFAULT_ADDRESS ";"		},
	{ IF_DEFAULT_ADDRESS ";;",				IF6_DEFAULT_ADDRESS ";;"		},
	{ IF_DEFAULT_ADDRESS ";239.192.0.1",			IF6_DEFAULT_ADDRESS ";ff08::1"			},
	{ IF_DEFAULT_ADDRESS ";239.192.0.1;239.192.0.1",	IF6_DEFAULT_ADDRESS ";ff08::1;ff08::1"		},
	{ IF_DEFAULT_ADDRESS ";PGM.MCAST.NET",			IF6_DEFAULT_ADDRESS ";IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_ADDRESS ";PGM.MCAST.NET;PGM.MCAST.NET",	IF6_DEFAULT_ADDRESS ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_ADDRESS ";239.192.0.1;PGM.MCAST.NET",	IF6_DEFAULT_ADDRESS ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_ADDRESS ";PGM.MCAST.NET;239.192.0.1",	IF6_DEFAULT_ADDRESS ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ IF_NETWORK,					/* ‡ */ IF6_NETWORK			},
	{ IF_NETWORK ";",				/* ‡ */ IF6_NETWORK ";"		},
	{ IF_NETWORK ";;",				/* ‡ */ IF6_NETWORK ";;"		},
	{ IF_NETWORK ";239.192.0.1",			/* ‡ */ IF6_NETWORK ";ff08::1"			},
	{ IF_NETWORK ";239.192.0.1;239.192.0.1",	/* ‡ */ IF6_NETWORK ";ff08::1;ff08::1"		},
	{ IF_NETWORK ";PGM.MCAST.NET",			/* ‡ */ IF6_NETWORK ";IP6-PGM.MCAST.NET"	},
	{ IF_NETWORK ";PGM.MCAST.NET;PGM.MCAST.NET",	/* ‡ */ IF6_NETWORK ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ IF_NETWORK ";239.192.0.1;PGM.MCAST.NET",	/* ‡ */ IF6_NETWORK ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ IF_NETWORK ";PGM.MCAST.NET;239.192.0.1",	/* ‡ */ IF6_NETWORK ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ IF_HOSTNAME,					IF6_HOSTNAME			},
	{ IF_HOSTNAME ";",				IF6_HOSTNAME ";"		},
	{ IF_HOSTNAME ";;",				IF6_HOSTNAME ";;"		},
	{ IF_HOSTNAME ";239.192.0.1",			IF6_HOSTNAME ";ff08::1"		},
	{ IF_HOSTNAME ";239.192.0.1;239.192.0.1",	IF6_HOSTNAME ";ff08::1;ff08::1"	},
	{ IF_HOSTNAME ";PGM.MCAST.NET",			IF6_HOSTNAME ";IP6-PGM.MCAST.NET" },
	{ IF_HOSTNAME ";PGM.MCAST.NET;PGM.MCAST.NET",	IF6_HOSTNAME ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET" },
	{ IF_HOSTNAME ";239.192.0.1;PGM.MCAST.NET",	IF6_HOSTNAME ";ff08::1;IP6-PGM.MCAST.NET" },
	{ IF_HOSTNAME ";PGM.MCAST.NET;239.192.0.1",	IF6_HOSTNAME ";IP6-PGM.MCAST.NET;ff08::1" },
};

START_TEST (test_parse_transport_001)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= (g_ai_family == AF_INET6) ? cases_001[_i].ipv6 : cases_001[_i].ipv4;
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	gsize			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	g_message ("%i: test_parse_transport_001(%s, \"%s\")", _i, (ai_family == AF_INET6) ? "AF_INET6" : ( (ai_family == AF_INET) ? "AF_INET" : "AF_UNSPEC" ), s);

/* † Multiple scoped IPv6 interfaces match a simple interface name network paramter and so
 *   pgm-if_parse_transport will fail finding multiple matching interfaces
 */
	if (AF_INET6 == ai_family && 0 == strncmp (s, IF_DEFAULT_INTERFACE, strlen(IF_DEFAULT_INTERFACE)))
	{
		g_message ("IPv6 exception, multiple scoped addresses on one interface");
		fail_unless (-ERANGE == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
		return;
	}

/* ‡ Linux does not support IPv6 /etc/networks so IPv6 entries appear as 255.255.255.255 and
 *   pgm_if_parse_transport will fail.
 */

	if (0 == strncmp (s, IF6_NETWORK, strlen(IF6_NETWORK)))
	{
		g_message ("IPv6 exception, /etc/networks not supported under Linux");
		fail_unless (-ENODEV == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
		return;
	}

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (G_N_ELEMENTS(recv_gsr) == recv_len);

	fail_unless (match_default_group (ai_family, recv_gsr));
	fail_unless (match_default_interface (ai_family, recv_gsr));
	fail_unless (match_default_group (ai_family, send_gsr));
	fail_unless (match_default_interface (ai_family, send_gsr));
}
END_TEST

/* network to node address in bits, 8-32
 *
 * e.g. 127.0.0.1/16
 */

static const if_test_case_t cases_002[] = {
	{ IF_DEFAULT_ADDRESS "/24",				IF6_DEFAULT_ADDRESS "/64"				},
	{ IF_DEFAULT_ADDRESS "/24;",				IF6_DEFAULT_ADDRESS "/64;"				},
	{ IF_DEFAULT_ADDRESS "/24;;",				IF6_DEFAULT_ADDRESS "/64;;"				},
	{ IF_DEFAULT_ADDRESS "/24;239.192.0.1",			IF6_DEFAULT_ADDRESS "/64;ff08::1"			},
	{ IF_DEFAULT_ADDRESS "/24;239.192.0.1;239.192.0.1",	IF6_DEFAULT_ADDRESS "/64;ff08::1;ff08::1"		},
	{ IF_DEFAULT_ADDRESS "/24;PGM.MCAST.NET",		IF6_DEFAULT_ADDRESS "/64;IP6-PGM.MCAST.NET"		},
	{ IF_DEFAULT_ADDRESS "/24;PGM.MCAST.NET;PGM.MCAST.NET",	IF6_DEFAULT_ADDRESS "/64;IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_ADDRESS "/24;239.192.0.1;PGM.MCAST.NET",	IF6_DEFAULT_ADDRESS "/64;ff08::1;IP6-PGM.MCAST.NET"	},
	{ IF_DEFAULT_ADDRESS "/24;PGM.MCAST.NET;239.192.0.1",	IF6_DEFAULT_ADDRESS "/64;IP6-PGM.MCAST.NET;ff08::1"	},
};

START_TEST (test_parse_transport_002)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= (g_ai_family == AF_INET6) ? cases_002[_i].ipv6 : cases_002[_i].ipv4;
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	gsize			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (G_N_ELEMENTS(recv_gsr) == recv_len);

	fail_unless (match_default_group (ai_family, recv_gsr));
	fail_unless (match_default_interface (ai_family, recv_gsr));
	fail_unless (match_default_group (ai_family, send_gsr));
	fail_unless (match_default_interface (ai_family, send_gsr));
}
END_TEST

/* asymmetric groups
 */

START_TEST (test_parse_transport_003)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= (g_ai_family == AF_INET6) ? ";ff08::1;ff08::2"
							       /* AF_INET */: ";239.192.56.1;239.192.56.2";
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[1];
	gsize			recv_len	= 1;
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (1 == recv_len);

	if (ai_family == AF_INET6)
	{
		struct sockaddr_storage addr;
		inet_pton (AF_INET6, "ff08::1", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr_in6*)&addr)->sin6_family = AF_INET6;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[0].gsr_group, (struct sockaddr*)&addr));

		inet_pton (AF_INET6, "ff08::2", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET6;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&send_gsr[0].gsr_group, (struct sockaddr*)&addr));
	}
	else
	{
		struct sockaddr_storage addr;
		inet_pton (AF_INET, "239.192.56.1", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[0].gsr_group, (struct sockaddr*)&addr));

		inet_pton (AF_INET, "239.192.56.2", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&send_gsr[0].gsr_group, (struct sockaddr*)&addr));
	}
	fail_unless (match_any_source (ai_family, recv_gsr));
	fail_unless (match_any_source (ai_family, send_gsr));
}
END_TEST

/* multiple receive groups and asymmetric sending
 */

START_TEST (test_parse_transport_004)
{
	fail_unless (g_ai_family == AF_UNSPEC || g_ai_family == AF_INET || g_ai_family == AF_INET6);

	const char*		s		= (g_ai_family == AF_INET6) ? ";ff08::1,ff08::2;ff08::3"
							       /* AF_INET */: ";239.192.56.1,239.192.56.2;239.192.56.3";
	int			ai_family	= g_ai_family;
	struct group_source_req	recv_gsr[2];
	gsize			recv_len	= 2;
	struct group_source_req send_gsr[1];

	fail_unless (0 == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
	fail_unless (2 == recv_len);

	if (ai_family == AF_INET6)
	{
		struct sockaddr_storage addr;
		inet_pton (AF_INET6, "ff08::1", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET6;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[0].gsr_group, (struct sockaddr*)&addr));
		inet_pton (AF_INET6, "ff08::2", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET6;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[1].gsr_group, (struct sockaddr*)&addr));

		inet_pton (AF_INET6, "ff08::3", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET6;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&send_gsr[0].gsr_group, (struct sockaddr*)&addr));
	}
	else
	{
		struct sockaddr_storage addr;
		inet_pton (AF_INET, "239.192.56.1", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[0].gsr_group, (struct sockaddr*)&addr));
		inet_pton (AF_INET, "239.192.56.2", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&recv_gsr[1].gsr_group, (struct sockaddr*)&addr));

		inet_pton (AF_INET, "239.192.56.3", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&send_gsr[0].gsr_group, (struct sockaddr*)&addr));
	}
	fail_unless (match_any_source (ai_family, recv_gsr));
	fail_unless (match_any_source (ai_family, send_gsr));
}
END_TEST


/* too many interfaces
 */
START_TEST (test_parse_transport_fail_000)
{
	const char*		s		= "eth0,lo;;;";
	int			ai_family	= AF_UNSPEC;
	struct group_source_req	recv_gsr[1];
	gsize			recv_len	= G_N_ELEMENTS(recv_gsr);
	struct group_source_req send_gsr[1];

	fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* invalid characters, or simply just bogus
 */
START_TEST (test_parse_transport_fail_001)
{
        const char*             s               = "!@#$%^&*()";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* too many groups
 */
START_TEST (test_parse_transport_fail_002)
{
        const char*             s               = ";239.192.0.1,239.192.0.2,239.192.0.3,239.192.0.4,239.192.0.5,239.192.0.6,239.192.0.7,239.192.0.8,239.192.0.9,239.192.0.10,239.192.0.11,239.192.0.12,239.192.0.13,239.192.0.14,239.192.0.15,239.192.0.16,239.192.0.17,239.192.0.18,239.192.0.19,239.192.0.20;239.192.0.21";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* too many receiver groups in asymmetric pairing
 */
START_TEST (test_parse_transport_fail_003)
{
        const char*             s               = ";239.192.0.1,239.192.0.2,239.192.0.3,239.192.0.4,239.192.0.5,239.192.0.6,239.192.0.7,239.192.0.8,239.192.0.9,239.192.0.10,239.192.0.11,239.192.0.12,239.192.0.13,239.192.0.14,239.192.0.15,239.192.0.16,239.192.0.17,239.192.0.18,239.192.0.19,239.192.0.20,239.192.0.21;239.192.0.22";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* null string
 */
START_TEST (test_parse_transport_fail_004)
{
        const char*             s               = NULL;
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* invalid address family
 */
START_TEST (test_parse_transport_fail_005)
{
        const char*             s               = ";";
        int                     ai_family       = AF_IPX;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* invalid receive group pointer
 */
START_TEST (test_parse_transport_fail_006)
{
        const char*             s               = ";";
        int                     ai_family       = AF_UNSPEC;
        gsize                     recv_len        = 1;            /* bogus but valid */
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, NULL, &recv_len, send_gsr));
}
END_TEST

/* zero length receive group size
 */
START_TEST (test_parse_transport_fail_007)
{
        const char*             s               = ";";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = 0;
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
}
END_TEST

/* invalid length receive group size
 */
START_TEST (test_parse_transport_fail_008)
{
        const char*             s               = ";";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        struct group_source_req send_gsr[1];

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, NULL, send_gsr));
}
END_TEST

/* invalid send group pointer
 */
START_TEST (test_parse_transport_fail_009)
{
        const char*             s               = ";";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = G_N_ELEMENTS(recv_gsr);

        fail_unless (-EINVAL == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, NULL));
}
END_TEST

/* more receive groups than supplied limit
 */
START_TEST (test_parse_transport_fail_010)
{
        const char*             s               = ";239.192.0.1,239.192.0.2;239.192.0.3";
        int                     ai_family       = AF_UNSPEC;
        struct group_source_req recv_gsr[1];
        gsize                     recv_len        = 1;
        struct group_source_req send_gsr[1];

        fail_unless (-ENOMEM == pgm_if_parse_transport (s, ai_family, recv_gsr, &recv_len, send_gsr));
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
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_000, 0, G_N_ELEMENTS(cases_000));
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_003);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_004);

	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_000);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_001);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_002);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_003);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_004);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_005);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_006);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_007);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_008);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_009);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_010);

/* forced IPv4 */
	suite_add_tcase (s, tc_parse_transport_ipv4);
	tcase_add_loop_test (tc_parse_transport_ipv4, test_parse_transport_000, 0, G_N_ELEMENTS(cases_000));
	tcase_add_loop_test (tc_parse_transport_ipv4, test_parse_transport_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_ipv4, test_parse_transport_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_003);
	tcase_add_test (tc_parse_transport_ipv4, test_parse_transport_004);

/* forced IPv6 */
	suite_add_tcase (s, tc_parse_transport_ipv6);
	tcase_add_loop_test (tc_parse_transport_ipv6, test_parse_transport_000, 0, G_N_ELEMENTS(cases_000));
	tcase_add_loop_test (tc_parse_transport_ipv6, test_parse_transport_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_ipv6, test_parse_transport_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_003);
	tcase_add_test (tc_parse_transport_ipv6, test_parse_transport_004);

	return s;
}

/* eof */

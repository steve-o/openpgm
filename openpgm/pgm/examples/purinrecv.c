/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * プリン PGM receiver
 *
 * Copyright (c) 2006-2010 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#ifdef _WIN32
#	include "getopt.h"
#endif
#include <pgm/pgm.h>


/* globals */

static int		port = 0;
static const char*	network = "";
static bool		use_multicast_loop = FALSE;
static int		udp_encap_port = 0;

static int		max_tpdu = 1500;
static int		sqns = 100;

static bool		use_fec = FALSE;
static int		rs_k = 8;
static int		rs_n = 255;

static pgm_transport_t* transport = NULL;
static bool		is_terminated = FALSE;

#ifndef _WIN32
static int		terminate_pipe[2];
static void on_signal (int);
#else
static HANDLE		terminate_event;
static BOOL on_console_ctrl (DWORD);
#endif

static void usage (const char*) __attribute__((__noreturn__));
static bool on_startup (void);
static int on_data (void*restrict, size_t, pgm_tsi_t*restrict);


static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -k <k>          : Configure Reed-Solomon code (n, k)\n");
	fprintf (stderr, "  -g <n>\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
	fprintf (stderr, "  -i              : List available interfaces\n");
	exit (EXIT_SUCCESS);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	pgm_error_t* pgm_err = NULL;

	setlocale (LC_ALL, "");

	puts ("プリン プリン");

	if (!pgm_init (&pgm_err)) {
		fprintf (stderr, "Unable to start PGM engine: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:f:k:g:lih")) != -1)
	{
		switch (c) {
		case 'n':	network = optarg; break;
		case 's':	port = atoi (optarg); break;
		case 'p':	udp_encap_port = atoi (optarg); break;
		case 'f':	use_fec = TRUE; break;
		case 'k':	rs_k = atoi (optarg); break;
		case 'g':	rs_n = atoi (optarg); break;
		case 'l':	use_multicast_loop = TRUE; break;

		case 'i':
			pgm_if_print_all();
			return EXIT_SUCCESS;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (use_fec && ( !rs_n || !rs_k )) {
		fprintf (stderr, "Invalid Reed-Solomon parameters RS(%d,%d).\n", rs_n, rs_k);
		usage (binary_name);
	}

/* setup signal handlers */
#ifdef SIGHUP
	signal (SIGHUP,  SIG_IGN);
#endif
#ifndef _WIN32
	int e = pipe (terminate_pipe);
	assert (0 == e);
	signal (SIGINT,  on_signal);
	signal (SIGTERM, on_signal);
#else
	terminate_event = CreateEvent (NULL, TRUE, FALSE, TEXT("TerminateEvent"));
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
#endif /* !_WIN32 */

	if (!on_startup()) {
		fprintf (stderr, "Startup failed\n");
		return EXIT_FAILURE;
	}

/* dispatch loop */
#ifndef _WIN32
	int fds;
	fd_set readfds;
#else
	int n_handles = 3;
	HANDLE waitHandles[ n_handles ];
	DWORD dwTimeout, dwEvents;
	WSAEVENT recvEvent, pendingEvent;

	recvEvent = WSACreateEvent ();
	WSAEventSelect (pgm_transport_get_recv_fd (transport), recvEvent, FD_READ);
	pendingEvent = WSACreateEvent ();
	WSAEventSelect (pgm_transport_get_pending_fd (transport), pendingEvent, FD_READ);

	waitHandles[0] = terminate_event;
	waitHandles[1] = recvEvent;
	waitHandles[2] = pendingEvent;
#endif /* !_WIN32 */
	puts ("Entering PGM message loop ... ");
	do {
		struct timeval tv;
		char buffer[4096];
		size_t len;
		pgm_tsi_t from;
		const int status = pgm_recvfrom (transport,
					         buffer,
					         sizeof(buffer),
					         0,
					         &len,
					         &from,
					         &pgm_err);
		switch (status) {
		case PGM_IO_STATUS_NORMAL:
			on_data (buffer, len, &from);
			break;
		case PGM_IO_STATUS_TIMER_PENDING:
			pgm_transport_get_timer_pending (transport, &tv);
			goto block;
		case PGM_IO_STATUS_RATE_LIMITED:
			pgm_transport_get_rate_remaining (transport, &tv);
		case PGM_IO_STATUS_WOULD_BLOCK:
/* select for next event */
block:
#ifndef _WIN32
			fds = terminate_pipe[0] + 1;
			FD_ZERO(&readfds);
			FD_SET(terminate_pipe[0], &readfds);
			pgm_transport_select_info (transport, &readfds, NULL, &fds);
			fds = select (fds, &readfds, NULL, NULL, PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
#else
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			dwEvents = WaitForMultipleObjects (n_handles, waitHandles, FALSE, dwTimeout);
			switch (dwEvents) {
			case WAIT_OBJECT_0+1: WSAResetEvent (recvEvent); break;
			case WAIT_OBJECT_0+2: WSAResetEvent (pendingEvent); break;
			default: break;
			}
#endif /* !_WIN32 */
			break;

		default:
			if (pgm_err) {
				fprintf (stderr, "%s\n", pgm_err->message);
				pgm_error_free (pgm_err);
				pgm_err = NULL;
			}
			if (PGM_IO_STATUS_ERROR == status)
				break;
		}
	} while (!is_terminated);

	puts ("Message loop terminated, cleaning up.");

/* cleanup */
#ifndef _WIN32
	close (terminate_pipe[0]);
	close (terminate_pipe[1]);
#else
	WSACloseEvent (recvEvent);
	WSACloseEvent (pendingEvent);
	CloseHandle (terminate_event);
#endif /* !_WIN32 */

	if (transport) {
		puts ("Destroying transport.");
		pgm_transport_destroy (transport, TRUE);
		transport = NULL;
	}

	puts ("PGM engine shutdown.");
	pgm_shutdown ();
	puts ("finished.");
	return EXIT_SUCCESS;
}

#ifndef _WIN32
static
void
on_signal (
	int		signum
	)
{
	printf ("on_signal (signum:%d)\n", signum);
	is_terminated = TRUE;
	const char one = '1';
	const size_t writelen = write (terminate_pipe[1], &one, sizeof(one));
	assert (sizeof(one) == writelen);
}
#else
static
BOOL
on_console_ctrl (
	DWORD		dwCtrlType
	)
{
	printf ("on_console_ctrl (dwCtrlType:%lu)\n", (unsigned long)dwCtrlType);
	is_terminated = TRUE;
	SetEvent (terminate_event);
	return TRUE;
}
#endif /* !_WIN32 */

static
bool
on_startup (void)
{
	struct pgm_transport_info_t* res = NULL;
	pgm_error_t* pgm_err = NULL;

	puts ("Create transport.");

/* parse network parameter into transport address structure */
	char network_param[1024];
	sprintf (network_param, "%s", network);
	if (!pgm_if_get_transport_info (network_param, NULL, &res, &pgm_err)) {
		fprintf (stderr, "Parsing network parameter: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		return FALSE;
	}
/* create global session identifier */
	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &pgm_err)) {
		fprintf (stderr, "Creating GSI: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	if (udp_encap_port) {
		res->ti_udp_encap_ucast_port = udp_encap_port;
		res->ti_udp_encap_mcast_port = udp_encap_port;
	}
	if (port)
		res->ti_dport = port;
	if (!pgm_transport_create (&transport, res, &pgm_err)) {
		fprintf (stderr, "Creating transport: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	pgm_if_free_transport_info (res);

/* set PGM parameters */
	pgm_transport_set_nonblocking (transport, TRUE);
	pgm_transport_set_recv_only (transport, TRUE, FALSE);
	pgm_transport_set_max_tpdu (transport, max_tpdu);
	pgm_transport_set_rxw_sqns (transport, sqns);
	pgm_transport_set_multicast_loop (transport, use_multicast_loop);
	pgm_transport_set_hops (transport, 16);
	pgm_transport_set_peer_expiry (transport, pgm_secs(300));
	pgm_transport_set_spmr_expiry (transport, pgm_msecs(250));
	pgm_transport_set_nak_bo_ivl (transport, pgm_msecs(50));
	pgm_transport_set_nak_rpt_ivl (transport, pgm_secs(2));
	pgm_transport_set_nak_rdata_ivl (transport, pgm_secs(2));
	pgm_transport_set_nak_data_retries (transport, 50);
	pgm_transport_set_nak_ncf_retries (transport, 50);
	if (use_fec) {
		pgm_transport_set_fec (transport, 0, TRUE, TRUE, rs_n, rs_k);
	}

/* assign transport to specified address */
	if (!pgm_transport_bind (transport, &pgm_err)) {
		fprintf (stderr, "Binding transport: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_transport_destroy (transport, FALSE);
		transport = NULL;
		return FALSE;
	}

	puts ("Startup complete.");
	return TRUE;
}

static
int
on_data (
	void*      restrict data,
	size_t		    len,
	pgm_tsi_t* restrict from
	)
{
/* protect against non-null terminated strings */
	char buf[1024], tsi[PGM_TSISTRLEN];
	snprintf (buf, sizeof(buf), "%s", (char*)data);
	pgm_tsi_print_r (from, tsi, sizeof(tsi));
	printf ("\"%s\" (%zu bytes from %s)\n",
			buf, len, tsi);
	return 0;
}

/* eof */

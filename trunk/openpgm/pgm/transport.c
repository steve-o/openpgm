/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM transport: manage incoming & outgoing sockets with ambient SPMs, 
 * transmit & receive windows.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/backtrace.h"
#include "pgm/log.h"
#include "pgm/packet.h"
#include "pgm/txwi.h"
#include "pgm/rxwi.h"
#include "pgm/transport.h"
#include "pgm/rate_control.h"
#include "pgm/sn.h"
#include "pgm/timer.h"

#define TRANSPORT_DEBUG
//#define TRANSPORT_SPM_DEBUG

#ifndef TRANSPORT_DEBUG
#	define g_trace(m,...)		while (0)
#else
#include <ctype.h>
#	ifdef TRANSPORT_SPM_DEBUG
#		define g_trace(m,...)		g_debug(__VA_ARGS__)
#	else
#		define g_trace(m,...)		do { if (strcmp((m),"SPM")) { g_debug(__VA_ARGS__); } } while (0)
#	endif
#endif


/* internal: Glib event loop GSource of spm & rx state timers */
struct pgm_timer_t {
	GSource		source;
	pgm_time_t	expiration;
	pgm_transport_t* transport;
};

typedef struct pgm_timer_t pgm_timer_t;


/* callback for pgm timer events */
typedef int (*pgm_timer_callback)(pgm_transport_t*);


/* global locals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN		"pgmtransport"

static int ipproto_pgm = IPPROTO_PGM;

GStaticRWLock pgm_transport_list_lock = G_STATIC_RW_LOCK_INIT;		/* list of all transports for admin interfaces */
GSList* pgm_transport_list = NULL;


/* helpers for pgm_peer_t */
#define next_nak_rb_expiry(r)       ( ((pgm_rxw_packet_t*)(r)->backoff_queue->tail->data)->nak_rb_expiry )
#define next_nak_rpt_expiry(r)      ( ((pgm_rxw_packet_t*)(r)->wait_ncf_queue->tail->data)->nak_rpt_expiry )
#define next_nak_rdata_expiry(r)    ( ((pgm_rxw_packet_t*)(r)->wait_data_queue->tail->data)->nak_rdata_expiry )


static GSource* pgm_create_timer (pgm_transport_t*);
static int pgm_add_timer_full (pgm_transport_t*, gint);
static int pgm_add_timer (pgm_transport_t*);

static gboolean pgm_timer_prepare (GSource*, gint*);
static gboolean pgm_timer_check (GSource*);
static gboolean pgm_timer_dispatch (GSource*, GSourceFunc, gpointer);

static GSourceFuncs g_pgm_timer_funcs = {
	pgm_timer_prepare,
	pgm_timer_check,
	pgm_timer_dispatch,
	NULL
};

static int send_spm_unlocked (pgm_transport_t*);
static inline int send_spm (pgm_transport_t*);
static int send_spmr (pgm_peer_t*);
static int send_nak (pgm_peer_t*, guint32);
static int send_nak_list (pgm_peer_t*, pgm_sqn_list_t*);
static int send_ncf (pgm_transport_t*, struct sockaddr*, struct sockaddr*, guint32);
static int send_ncf_list (pgm_transport_t*, struct sockaddr*, struct sockaddr*, pgm_sqn_list_t*);

static void nak_rb_state (gpointer, gpointer);
static void nak_rpt_state (gpointer, gpointer);
static void nak_rdata_state (gpointer, gpointer);
static void check_peer_nak_state (pgm_transport_t*);
static pgm_time_t min_nak_expiry (pgm_time_t, pgm_transport_t*);

static int send_rdata (pgm_transport_t*, int, gpointer, int);

static inline pgm_peer_t* pgm_peer_ref (pgm_peer_t*);
static inline void pgm_peer_unref (pgm_peer_t*);

static int on_pgm_data (gpointer, guint, gpointer);

static gboolean on_nak_pipe (GIOChannel*, GIOCondition, gpointer);
static gboolean on_timer_pipe (GIOChannel*, GIOCondition, gpointer);

static int on_spm (pgm_peer_t*, struct pgm_header*, char*, int);
static int on_spmr (pgm_transport_t*, pgm_peer_t*, struct pgm_header*, char*, int);
static int on_nak (pgm_transport_t*, pgm_tsi_t*, struct pgm_header*, char*, int);
static int on_peer_nak (pgm_peer_t*, struct pgm_header*, char*, int);
static int on_ncf (pgm_peer_t*, struct pgm_header*, char*, int);
static int on_nnak (pgm_transport_t*, pgm_tsi_t*, struct pgm_header*, char*, int);
static int on_odata (pgm_peer_t*, struct pgm_header*, char*, int);
static int on_rdata (pgm_peer_t*, struct pgm_header*, char*, int);


gchar*
pgm_print_tsi (
	const pgm_tsi_t*	tsi
	)
{
	guint8* gsi = (guint8*)tsi;
	guint16 source_port = tsi->sport;
	static char buf[sizeof("000.000.000.000.000.000.00000")];
	snprintf(buf, sizeof(buf), "%i.%i.%i.%i.%i.%i.%i",
		gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (source_port));
	return buf;
}

/* convert a transport session identifier TSI to a hash value
 *  */

inline guint
pgm_tsi_hash (
	gconstpointer v
        )
{
	return g_str_hash(pgm_print_tsi((pgm_tsi_t*)v));
}

/* compare two transport session identifier TSI values and return TRUE if they are equal
 *  */

inline gint
pgm_tsi_equal (
	gconstpointer   v,
	gconstpointer   v2
        )
{
	return memcmp (v, v2, (6 * sizeof(guint8)) + sizeof(guint16)) == 0;
}

static inline guint32 nak_rb_ivl (pgm_transport_t* transport)
{
	return g_rand_int_range (transport->rand_, 1 /* us */, transport->nak_bo_ivl);
}

static inline gpointer pgm_sqn_list_alloc (pgm_transport_t* t)
{
	pgm_sqn_list_t* p = t->trash_rdata ? g_trash_stack_pop (&t->trash_rdata) : g_slice_alloc (sizeof(pgm_sqn_list_t));
	p->len = 0;
	return p;
}

gpointer pgm_alloc (pgm_transport_t* transport)
{
	g_static_rw_lock_writer_lock (&transport->txw_lock);
	gpointer ptr = ((gchar*)pgm_txw_alloc (transport->txw)) + sizeof(struct pgm_header) + sizeof(struct pgm_data);
	g_static_rw_lock_writer_unlock (&transport->txw_lock);
	return ptr;
}

static inline int pgm_transport_send_one_copy_unlocked (pgm_transport_t* transport, const gchar* buf, gsize count, int flags)
{
	int retval = 0;

	if (flags & MSG_DONTWAIT)
	{
	        int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + count;
		int retval = pgm_rate_check (transport->rate_control, tpdu_length, flags);
		if (retval == -1) {
			goto out;
		}
	}

	gchar *pkt = ((gchar*)pgm_txw_alloc (transport->txw)) + sizeof(struct pgm_header) + sizeof(struct pgm_data);
	memcpy (pkt, buf, count);
	retval = pgm_transport_send_one_unlocked (transport, pkt, count, flags);

out:
	return retval;
}

/* locked and rate regulated sendto
 */

static inline ssize_t
pgm_sendto (pgm_transport_t* transport, gboolean ra, const void* buf, size_t len, int flags, const struct sockaddr* to, socklen_t tolen)
{
	int retval;
	GStaticMutex* mutex = ra ? &transport->send_with_router_alert_mutex : &transport->send_mutex;
	int sock = ra ? transport->send_with_router_alert_sock : transport->send_sock;

	g_static_mutex_lock (mutex);
	pgm_rate_check (transport->rate_control, len, 0);
	retval = sendto (sock, buf, len, flags, to, tolen);
	g_static_mutex_unlock (mutex);

	return retval;
}

int
pgm_set_nonblocking (int filedes[2])
{
	int retval = 0;

/* set write end non-blocking */
	int fd_flags = fcntl (filedes[1], F_GETFL);
	if (fd_flags < 0) {
		retval = errno;
		goto out;
	}
	retval = fcntl (filedes[1], F_SETFL, fd_flags | O_NONBLOCK);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
/* set read end non-blocking */
	fcntl (filedes[0], F_GETFL);
	if (fd_flags < 0) {
		retval = errno;
		goto out;
	}
	retval = fcntl (filedes[0], F_SETFL, fd_flags | O_NONBLOCK);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

out:
	return retval;
}		

/* startup PGM engine, mainly finding PGM protocol definition, if any from NSS
 */

int
pgm_init (void)
{
	int retval = 0;

/* ensure threading enabled */
	if (!g_thread_supported ()) g_thread_init (NULL);

/* ensure timer enabled */
	if (!pgm_time_supported ()) pgm_time_init();


/* find PGM protocol id */

// TODO: fix valgrind errors
#if HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	e = getprotobyname_r("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace("INFO","Setting PGM protocol number to %i from /etc/protocols.");
			ipproto_pgm = proto->p_proto;
		}
	}
#else
	struct protoent *proto = getprotobyname("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace("INFO","Setting PGM protocol number to %i from /etc/protocols.", proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#endif

	return retval;
}

/* destroy a pgm_transport object and contents, if last transport also destroy
 * associated event loop
 *
 * TODO: clear up locks on destruction: 1: flushing, 2: destroying:, 3: destroyed.
 *
 * If application calls a function on the transport after destroy() it is a
 * programmer error: segv likely to occur on unlock.
 */

int
pgm_transport_destroy (
	pgm_transport_t*	transport,
	gboolean		flush
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	g_static_rw_lock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = g_slist_remove (pgm_transport_list, transport);
	g_static_rw_lock_writer_unlock (&pgm_transport_list_lock);

/* terminate & join internal thread */
#ifndef PGM_SINGLE_THREAD
	if (transport->timer_thread) {
		g_main_loop_quit (transport->timer_loop);
	}
	if (transport->timer_thread) {
		g_main_loop_quit (transport->timer_loop);
		g_thread_join (transport->timer_thread);
		transport->timer_thread = NULL;
	}
#endif /* !PGM_SINGLE_THREAD */

	g_static_mutex_lock (&transport->mutex);

/* assume lock from create() if not bound */
	if (transport->bound) {
		g_static_mutex_lock (&transport->send_mutex);
		g_static_mutex_lock (&transport->send_with_router_alert_mutex);
	}

/* flush data by sending heartbeat SPMs & processing NAKs until ambient */
	if (flush) {
	}

	if (transport->peers_hashtable) {
		g_hash_table_destroy (transport->peers_hashtable);
		transport->peers_hashtable = NULL;
	}
	if (transport->peers_list) {
		g_trace ("INFO","destroying peer data.");

		do {
			GList* next = transport->peers_list->next;
			pgm_peer_unref ((pgm_peer_t*)transport->peers_list->data);

			transport->peers_list = next;
		} while (transport->peers_list);
	}

/* clean up receiver trash stacks */
	if (transport->rx_data) {
		gpointer* p = NULL;
		while ( (p = g_trash_stack_pop (&transport->rx_data)) )
		{
			g_slice_free1 (transport->max_tpdu - transport->iphdr_len, p);
		}

		g_assert (transport->rx_data == NULL);
	}

	if (transport->rx_packet) {
		gpointer* p = NULL;
		while ( (p = g_trash_stack_pop (&transport->rx_packet)) )
		{
			g_slice_free1 (sizeof(pgm_rxw_packet_t), p);
		}

		g_assert (transport->rx_packet == NULL);
	}

	if (transport->txw) {
		g_trace ("INFO","destroying transmit window.");
		pgm_txw_shutdown (transport->txw);
		transport->txw = NULL;
	}

	if (transport->rate_control) {
		g_trace ("INFO","destroying rate control.");
		pgm_rate_destroy (transport->rate_control);
		transport->rate_control = NULL;
	}
	if (transport->recv_sock) {
		g_trace ("INFO","closing receive socket.");
		close(transport->recv_sock);
		transport->recv_sock = 0;
	}

	if (transport->send_sock) {
		g_trace ("INFO","closing send socket.");
		close(transport->send_sock);
		transport->send_sock = 0;
	}
	if (transport->send_with_router_alert_sock) {
		g_trace ("INFO","closing send with router alert socket.");
		close(transport->send_with_router_alert_sock);
		transport->send_with_router_alert_sock = 0;
	}

	if (transport->spm_heartbeat_interval) {
		g_free (transport->spm_heartbeat_interval);
		transport->spm_heartbeat_interval = NULL;
	}

	if (transport->rand_) {
		g_rand_free (transport->rand_);
		transport->rand_ = NULL;
	}
	if (transport->rdata_queue) {
		g_async_queue_unref (transport->rdata_queue);
		transport->rdata_queue = NULL;
	}
	if (transport->rdata_pipe[0]) {
		close (transport->rdata_pipe[0]);
		transport->rdata_pipe[0] = 0;
	}
	if (transport->rdata_pipe[1]) {
		close (transport->rdata_pipe[1]);
		transport->rdata_pipe[1] = 0;
	}
	if (transport->trash_rdata) {
		gpointer *p = NULL;
		while ( (p = g_trash_stack_pop (&transport->trash_rdata)) )
		{
			g_slice_free1 (sizeof(pgm_sqn_list_t), p);
		}

		g_assert (transport->trash_rdata == NULL);
	}
	if (transport->timer_pipe[0]) {
		close (transport->timer_pipe[0]);
		transport->timer_pipe[0] = 0;
	}
	if (transport->timer_pipe[1]) {
		close (transport->timer_pipe[1]);
		transport->timer_pipe[1] = 0;
	}

	if (transport->waiting_pipe[0]) {
		close (transport->waiting_pipe[0]);
		transport->waiting_pipe[0] = 0;
	}
	if (transport->waiting_pipe[1]) {
		close (transport->waiting_pipe[1]);
		transport->waiting_pipe[1] = 1;
	}

	g_static_rw_lock_free (&transport->peers_lock);

	g_static_rw_lock_free (&transport->txw_lock);

	g_static_mutex_free (&transport->rdata_mutex);

	if (transport->bound) {
		g_static_mutex_unlock (&transport->send_mutex);
		g_static_mutex_unlock (&transport->send_with_router_alert_mutex);
	}
	g_static_mutex_free (&transport->send_mutex);
	g_static_mutex_free (&transport->send_with_router_alert_mutex);

	g_static_mutex_unlock (&transport->mutex);
	g_static_mutex_free (&transport->mutex);

	if (transport->rx_buffer) {
		g_free (transport->rx_buffer);
		transport->rx_buffer = NULL;
	}

	if (transport->piov) {
		g_free (transport->piov);
		transport->piov = NULL;
	}

	g_free (transport);

	g_trace ("INFO","finished.");
	return 0;
}

static inline pgm_peer_t*
pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
	g_return_val_if_fail (peer != NULL, NULL);

	g_atomic_int_inc (&peer->ref_count);

	return peer;
}

static inline void
pgm_peer_unref (
	pgm_peer_t*	peer
	)
{
	gboolean is_zero;

	g_return_if_fail (peer != NULL);

	is_zero = g_atomic_int_dec_and_test (&peer->ref_count);

	if (G_UNLIKELY (is_zero))
	{
		pgm_rxw_shutdown (peer->rxw);
		peer->rxw = NULL;
		g_free (peer);
	}
}

static gpointer
pgm_timer_thread (
	gpointer		data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;

	transport->timer_context = g_main_context_new ();
	g_mutex_lock (transport->thread_mutex);
	transport->timer_loop = g_main_loop_new (transport->timer_context, FALSE);
	g_cond_signal (transport->thread_cond);
	g_mutex_unlock (transport->thread_mutex);

	g_trace ("INFO", "pgm_timer_thread entering event loop.");
	g_main_loop_run (transport->timer_loop);
	g_trace ("INFO", "pgm_timer_thread leaving event loop.");

/* cleanup */
	g_main_loop_unref (transport->timer_loop);
	g_main_context_unref (transport->timer_context);

	return NULL;
}

/* create a pgm_transport object.  create sockets that require superuser priviledges, if this is
 * the first instance also create a real-time priority receiving thread.  if interface ports
 * are specified then UDP encapsulation will be used instead of raw protocol.
 *
 * if send == recv only two sockets need to be created iff ip headers are not required (IPv6).
 *
 * all receiver addresses must be the same family.
 * interface and multiaddr must be the same family.
 */

#if ( AF_INET != PF_INET ) || ( AF_INET6 != PF_INET6 )
#error AF_INET and PF_INET are different values, the bananas are jumping in their pyjamas!
#endif

int
pgm_transport_create (
	pgm_transport_t**	transport_,
	pgm_gsi_t*		gsi,
	guint16			dport,
	struct pgm_sock_mreq*	recv_smr,	/* receive port, multicast group & interface address */
	int			recv_len,
	struct pgm_sock_mreq*	send_smr	/* send ... */
	)
{
	guint16 udp_encap_port = ((struct sockaddr_in*)&send_smr->smr_multiaddr)->sin_port;

	g_return_val_if_fail (transport_ != NULL, -EINVAL);
	g_return_val_if_fail (recv_smr != NULL, -EINVAL);
	g_return_val_if_fail (recv_len > 0, -EINVAL);
	g_return_val_if_fail (recv_len <= IP_MAX_MEMBERSHIPS, -EINVAL);
	g_return_val_if_fail (send_smr != NULL, -EINVAL);
	for (int i = 0; i < recv_len; i++)
	{
		g_return_val_if_fail (pgm_sockaddr_family(&recv_smr[i].smr_multiaddr) == pgm_sockaddr_family(&recv_smr[0].smr_multiaddr), -EINVAL);
		g_return_val_if_fail (pgm_sockaddr_family(&recv_smr[i].smr_multiaddr) == pgm_sockaddr_family(&recv_smr[i].smr_interface), -EINVAL);
	}
	g_return_val_if_fail (pgm_sockaddr_family(&send_smr->smr_multiaddr) == pgm_sockaddr_family(&send_smr->smr_interface), -EINVAL);

	int retval = 0;
	pgm_transport_t* transport;

/* create transport object */
	transport = g_malloc0 (sizeof(pgm_transport_t));

/* regular send lock */
	g_static_mutex_init (&transport->send_mutex);

/* IP router alert send lock */
	g_static_mutex_init (&transport->send_with_router_alert_mutex);

/* timer lock */
	g_static_mutex_init (&transport->mutex);

/* rdata queue locks */
	g_static_mutex_init (&transport->rdata_mutex);

/* transmit window read/write lock */
	g_static_rw_lock_init (&transport->txw_lock);

/* peer hash map & list lock */
	g_static_rw_lock_init (&transport->peers_lock);

/* lock tx until bound */
	g_static_mutex_lock (&transport->send_mutex);

	memcpy (&transport->tsi.gsi, gsi, 6);
	transport->dport = g_htons (dport);
	do {
		transport->tsi.sport = g_htons (g_random_int_range (0, UINT16_MAX));
	} while (transport->tsi.sport == transport->dport);

/* network data ports */
	transport->udp_encap_port = udp_encap_port;

/* copy network parameters */
	memcpy (&transport->send_smr, send_smr, sizeof(struct pgm_sock_mreq));
	for (int i = 0; i < recv_len; i++)
	{
		memcpy (&transport->recv_smr[i], &recv_smr[i], sizeof(struct pgm_sock_mreq));
	}
	transport->recv_smr_len = recv_len;

/* open sockets to implement PGM */
	int socket_type, protocol;
	if (transport->udp_encap_port) {
		g_trace ("INFO", "opening UDP encapsulated sockets.");
		socket_type = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
	} else {
		g_trace ("INFO", "opening raw sockets.");
		socket_type = SOCK_RAW;
		protocol = ipproto_pgm;
	}

	if ((transport->recv_sock = socket(pgm_sockaddr_family(&recv_smr[0].smr_interface),
						socket_type,
						protocol)) < 0)
	{
		retval = errno;
		if (retval == EPERM && 0 != getuid()) {
			g_critical ("PGM protocol requires this program to run as superuser.");
		}
		goto err_destroy;
	}

	if ((transport->send_sock = socket(pgm_sockaddr_family(&send_smr->smr_interface),
						socket_type,
						protocol)) < 0)
	{
		retval = errno;
		goto err_destroy;
	}

	if ((transport->send_with_router_alert_sock = socket(pgm_sockaddr_family(&send_smr->smr_interface),
						socket_type,
						protocol)) < 0)
	{
		retval = errno;
		goto err_destroy;
	}

/* create timer thread */
#ifndef PGM_SINGLE_THREAD
	GError* err;
	GThread* thread;

/* set up condition for thread context & loop being ready */
	transport->thread_mutex = g_mutex_new ();
	transport->thread_cond = g_cond_new ();

	thread = g_thread_create_full (pgm_timer_thread,
					transport,
					0,
					TRUE,
					TRUE,
					G_THREAD_PRIORITY_HIGH,
					&err);
	if (thread) {
		transport->timer_thread = thread;
	} else {
		g_error ("thread failed: %i %s", err->code, err->message);
		goto err_destroy;
	}

	g_mutex_lock (transport->thread_mutex);
	while (!transport->timer_loop)
		g_cond_wait (transport->thread_cond, transport->thread_mutex);
	g_mutex_unlock (transport->thread_mutex);

	g_mutex_free (transport->thread_mutex);
	transport->thread_mutex = NULL;
	g_cond_free (transport->thread_cond);
	transport->thread_cond = NULL;

#endif /* !PGM_SINGLE_THREAD */

	*transport_ = transport;

	g_static_rw_lock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = g_slist_append (pgm_transport_list, transport);
	g_static_rw_lock_writer_unlock (&pgm_transport_list_lock);

	return retval;

err_destroy:
	if (transport->thread_mutex) {
		g_mutex_free (transport->thread_mutex);
		transport->thread_mutex = NULL;
	}
	if (transport->thread_cond) {
		g_cond_free (transport->thread_cond);
		transport->thread_cond = NULL;
	}
	if (transport->timer_thread) {
	}
		
	if (transport->recv_sock) {
		close(transport->recv_sock);
		transport->recv_sock = 0;
	}
	if (transport->send_sock) {
		close(transport->send_sock);
		transport->send_sock = 0;
	}
	if (transport->send_with_router_alert_sock) {
		close(transport->send_with_router_alert_sock);
		transport->send_with_router_alert_sock = 0;
	}

	g_static_mutex_free (&transport->mutex);
	g_free (transport);
	transport = NULL;

	return retval;
}

/* drop out of setuid 0 */
void
pgm_drop_superuser (void)
{
	if (0 == getuid()) {
		setuid((gid_t)65534);
		setgid((uid_t)65534);
	}
}

/* 0 < tpdu < 65536 by data type (guint16)
 *
 * IPv4:   68 <= tpdu < 65536		(RFC 2765)
 * IPv6: 1280 <= tpdu < 65536		(RFC 2460)
 */

int
pgm_transport_set_max_tpdu (
	pgm_transport_t*	transport,
	guint16			max_tpdu
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_tpdu >= (sizeof(struct iphdr) + sizeof(struct pgm_header)), -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->max_tpdu = max_tpdu;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < hops < 256, hops == -1 use kernel default (ignored).
 */

int
pgm_transport_set_hops (
	pgm_transport_t*	transport,
	gint			hops
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (hops > 0, -EINVAL);
	g_return_val_if_fail (hops < 256, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->hops = hops;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* Linux 2.6 limited to millisecond resolution with conventional timers, however RDTSC
 * and future high-resolution timers allow nanosecond resolution.  Current ethernet technology
 * is limited to microseconds at best so we'll sit there for a bit.
 */

int
pgm_transport_set_ambient_spm (
	pgm_transport_t*	transport,
	guint			spm_ambient_interval	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (spm_ambient_interval > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->spm_ambient_interval = spm_ambient_interval;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* an array of intervals appropriately tuned till ambient period is reached.
 *
 * array is zero leaded for ambient state, and zero terminated for easy detection.
 */

int
pgm_transport_set_heartbeat_spm (
	pgm_transport_t*	transport,
	guint*			spm_heartbeat_interval,
	int			len
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (len > 0, -EINVAL);
	for (int i = 0; i < len; i++) {
		g_return_val_if_fail (spm_heartbeat_interval[i] > 0, -EINVAL);
	}

	g_static_mutex_lock (&transport->mutex);
	if (transport->spm_heartbeat_interval)
		g_free (transport->spm_heartbeat_interval);
	transport->spm_heartbeat_interval = g_malloc (sizeof(guint) * (len+2));
	memcpy (&transport->spm_heartbeat_interval[1], spm_heartbeat_interval, sizeof(guint) * len);
	transport->spm_heartbeat_interval[0] = transport->spm_heartbeat_interval[len] = 0;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* set interval timer & expiration timeout for peer expiration, very lax checking.
 *
 * 0 < 2 * spm_ambient_interval <= peer_expiry
 */

int
pgm_transport_set_peer_expiry (
	pgm_transport_t*	transport,
	guint			peer_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (peer_expiry > 0, -EINVAL);
	g_return_val_if_fail (peer_expiry >= 2 * transport->spm_ambient_interval, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->peer_expiry = peer_expiry;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* set maximum back off range for listening for multicast SPMR
 *
 * 0 < spmr_expiry < spm_ambient_interval
 */

int
pgm_transport_set_spmr_expiry (
	pgm_transport_t*	transport,
	guint			spmr_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (spmr_expiry > 0, -EINVAL);
	g_return_val_if_fail (transport->spm_ambient_interval > spmr_expiry, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->spmr_expiry = spmr_expiry;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < txw_preallocate <= txw_sqns 
 *
 * can only be enforced at bind.
 */

int
pgm_transport_set_txw_preallocate (
	pgm_transport_t*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->txw_preallocate = sqns;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < txw_sqns < one less than half sequence space
 */

int
pgm_transport_set_txw_sqns (
	pgm_transport_t*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->txw_sqns = sqns;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < secs < ( txw_sqns / txw_max_rte )
 *
 * can only be enforced upon bind.
 */

int
pgm_transport_set_txw_secs (
	pgm_transport_t*	transport,
	guint			secs
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (secs > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->txw_secs = secs;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < txw_max_rte < interface capacity
 *
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 *
 * no practical way to determine upper limit and enforce.
 */

int
pgm_transport_set_txw_max_rte (
	pgm_transport_t*	transport,
	guint			max_rte
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_rte > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->txw_max_rte = max_rte;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rxw_preallocate <= rxw_sqns 
 *
 * can only be enforced at bind.
 */

int
pgm_transport_set_rxw_preallocate (
	pgm_transport_t*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_preallocate = sqns;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rxw_sqns < one less than half sequence space
 */

int
pgm_transport_set_rxw_sqns (
	pgm_transport_t*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_sqns = sqns;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < secs < ( rxw_sqns / rxw_max_rte )
 *
 * can only be enforced upon bind.
 */

int
pgm_transport_set_rxw_secs (
	pgm_transport_t*	transport,
	guint			secs
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (secs > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_secs = secs;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rxw_max_rte < interface capacity
 *
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 *
 * no practical way to determine upper limit and enforce.
 */

int
pgm_transport_set_rxw_max_rte (
	pgm_transport_t*	transport,
	guint			max_rte
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_rte > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_max_rte = max_rte;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}


/* 0 < wmem < wmem_max (user)
 *
 * operating system and sysctl dependent maximum, minimum on Linux 256 (doubled).
 */

int
pgm_transport_set_sndbuf (
	pgm_transport_t*	transport,
	int			size
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (size > 0, -EINVAL);

	guint wmem_max;
	FILE *fp = fopen ("/proc/sys/net/core/wmem_max", "r");
	if (fp) {
		fscanf (fp, "%u", &wmem_max);
		fclose (fp);
		g_return_val_if_fail (size <= wmem_max, -EINVAL);
	} else {
		g_warning ("cannot open /proc/sys/net/core/wmem_max");
	}

	g_static_mutex_lock (&transport->mutex);
	transport->sndbuf = size;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rmem < rmem_max (user)
 */

int
pgm_transport_set_rcvbuf (
	pgm_transport_t*	transport,
	int			size
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (size > 0, -EINVAL);

	guint rmem_max;
	FILE *fp = fopen ("/proc/sys/net/core/rmem_max", "r");
	if (fp) {
		fscanf (fp, "%u", &rmem_max);
		fclose (fp);
		g_return_val_if_fail (size <= rmem_max, -EINVAL);
	} else {
		g_warning ("cannot open /proc/sys/net/core/rmem_max");
	}

	g_static_mutex_lock (&transport->mutex);
	transport->rcvbuf = size;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_bo_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_bo_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_rpt_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_rpt_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_rdata_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_rdata_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_data_retries (
	pgm_transport_t*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_data_retries = cnt;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_ncf_retries (
	pgm_transport_t*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_ncf_retries = cnt;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* context aware g_io helpers
 */
static guint
g_io_add_watch_context_full (
	GIOChannel*		channel,
	GMainContext*		context,
	gint			priority,
	GIOCondition		condition,
	GIOFunc			function,
	gpointer		user_data,
	GDestroyNotify		notify
	)
{
	GSource *source;
	guint id;
  
	g_return_val_if_fail (channel != NULL, 0);

	source = g_io_create_watch (channel, condition);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);
	g_source_set_callback (source, (GSourceFunc)function, user_data, notify);

	id = g_source_attach (source, context);
	g_source_unref (source);

	return id;
}

/* bind the sockets to the link layer to start receiving data.
 */

int
pgm_transport_bind (
	pgm_transport_t*	transport
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	int retval = 0;

	g_static_mutex_lock (&transport->mutex);

	g_trace ("INFO","creating new random number generator.");
	transport->rand_ = g_rand_new();

/* receiver to timer thread queue, aka RDATA todo list */
	g_trace ("INFO","create asynchronous receiver to timer queue.");
	transport->rdata_queue = g_async_queue_new();

	g_trace ("INFO","create rdata pipe.");
	retval = pipe (transport->rdata_pipe);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	g_trace ("INFO","create timer pipe.");
	retval = pipe (transport->timer_pipe);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	g_trace ("INFO","create waiting pipe.");
	retval = pipe (transport->waiting_pipe);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

	retval = pgm_set_nonblocking (transport->rdata_pipe);
	if (retval) {
		goto out;
	}
	retval = pgm_set_nonblocking (transport->timer_pipe);
	if (retval < 0) {
		goto out;
	}
	retval = pgm_set_nonblocking (transport->waiting_pipe);
	if (retval < 0) {
		goto out;
	}
		
	g_trace ("INFO","construct transmit window.");
	transport->txw = pgm_txw_init (transport->max_tpdu - sizeof(struct iphdr),
					transport->txw_preallocate,
					transport->txw_sqns,
					transport->txw_secs,
					transport->txw_max_rte);

/* create peer list */
	transport->peers_hashtable = g_hash_table_new (pgm_tsi_hash, pgm_tsi_equal);

	if (!transport->udp_encap_port)
	{
/* include IP header only for incoming data */
		retval = pgm_sockaddr_hdrincl (transport->recv_sock, pgm_sockaddr_family(&transport->recv_smr[0].smr_interface), TRUE);
		if (retval < 0) {
			retval = errno;
			goto out;
		}
	}

/* buffers, set size first then re-read to confirm actual value */
	if (transport->rcvbuf)
	{
		retval = setsockopt(transport->recv_sock, SOL_SOCKET, SO_RCVBUF, (char*)&transport->rcvbuf, sizeof(transport->rcvbuf));
		if (retval < 0) {
			retval = errno;
			goto out;
		}
	}
	if (transport->sndbuf)
	{
		retval = setsockopt(transport->send_sock, SOL_SOCKET, SO_SNDBUF, (char*)&transport->sndbuf, sizeof(transport->sndbuf));
		if (retval < 0) {
			retval = errno;
			goto out;
		}
		retval = setsockopt(transport->send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, (char*)&transport->sndbuf, sizeof(transport->sndbuf));
		if (retval < 0) {
			retval = errno;
			goto out;
		}
	}

	int buffer_size;
	socklen_t len = sizeof(buffer_size);
	retval = getsockopt(transport->recv_sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, &len);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	g_trace ("INFO","receive buffer set at %i bytes.", buffer_size);

	retval = getsockopt(transport->send_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = getsockopt(transport->send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	g_trace ("INFO","send buffer set at %i bytes.", buffer_size);

/* bind udp unicast sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms so callee should specify any interface.
 *
 * after binding default interfaces (0.0.0.0) are resolved
 */
/* TODO: different ports requires a new bound socket */
	retval = bind (transport->recv_sock,
			(struct sockaddr*)&transport->recv_smr[0].smr_interface,
			pgm_sockaddr_len(&transport->recv_smr[0].smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->recv_smr[0].smr_interface, s, sizeof(s));
		g_trace ("INFO","bind failed on recv_smr[0] %s", s);
#endif
		goto out;
	}

/* resolve bound address if 0.0.0.0 */
	if (((struct sockaddr_in*)&transport->recv_smr[0].smr_interface)->sin_addr.s_addr == INADDR_ANY)
	{
		char hostname[NI_MAXHOST + 1];
		gethostname (hostname, sizeof(hostname));
		struct hostent *he = gethostbyname (hostname);
		if (he == NULL) {
			retval = errno;
			g_trace ("INFO","gethostbyname failed on local hostname");
			goto out;
		}

		((struct sockaddr_in*)(&transport->recv_smr[0].smr_interface))->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->recv_smr[0].smr_interface, s, sizeof(s));
		g_trace ("INFO","bind succeeded on recv_smr[0] %s", s);
	}
#endif

	retval = bind (transport->send_sock,
			(struct sockaddr*)&transport->send_smr.smr_interface,
			pgm_sockaddr_len(&transport->send_smr.smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("INFO","bind failed on send_smr %s", s);
#endif
		goto out;
	}

/* resolve bound address if 0.0.0.0 */
	if (((struct sockaddr_in*)&transport->send_smr.smr_interface)->sin_addr.s_addr == INADDR_ANY)
	{
		char hostname[NI_MAXHOST + 1];
		gethostname (hostname, sizeof(hostname));
		struct hostent *he = gethostbyname (hostname);
		if (he == NULL) {
			retval = errno;
			g_trace ("INFO","gethostbyname failed on local hostname");
			goto out;
		}

		((struct sockaddr_in*)&transport->send_smr.smr_interface)->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("INFO","bind succeeded on send_smr %s", s);
	}
#endif

	retval = bind (transport->send_with_router_alert_sock,
			(struct sockaddr*)&transport->send_smr.smr_interface,
			pgm_sockaddr_len(&transport->send_smr.smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("INFO","bind (router alert) failed on send_smr %s", s);
#endif
		goto out;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("INFO","bind (router alert) succeeded on send_smr %s", s);
	}
#endif

/* receiving groups (multiple) */
/* TODO: add IPv6 multicast membership? */
	for (int i = 0; i < transport->recv_smr_len; i++)
	{
		struct pgm_sock_mreq* p = &transport->recv_smr[i];
		retval = pgm_sockaddr_add_membership (transport->recv_sock, p);
		if (retval < 0) {
			retval = errno;
#ifdef TRANSPORT_DEBUG
			char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop (&p->smr_multiaddr, s1, sizeof(s1));
			pgm_sockaddr_ntop (&p->smr_interface, s2, sizeof(s2));
			g_trace ("INFO","sockaddr_add_membership failed on recv_smr[%i] %s %s", i, s1, s2);
#endif
			goto out;
		}
#ifdef TRANSPORT_DEBUG
		else
		{
			char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop (&p->smr_multiaddr, s1, sizeof(s1));
			pgm_sockaddr_ntop (&p->smr_interface, s2, sizeof(s2));
			g_trace ("INFO","sockaddr_add_membership succeeded on recv_smr[%i] %s %s", i, s1, s2);
		}
#endif
	}

/* send group (singular) */
	retval = pgm_sockaddr_multicast_if (transport->send_sock, &transport->send_smr);
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("INFO","sockaddr_multicast_if failed on send_smr %s %s", s1, s2);
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("INFO","sockaddr_multicast_if succeeded on send_smr %s %s", s1, s2);
	}
#endif
	retval = pgm_sockaddr_multicast_if (transport->send_with_router_alert_sock, &transport->send_smr);
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("INFO","sockaddr_multicast_if (router alert) failed on send_smr %s %s", s1, s2);
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		pgm_sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("INFO","sockaddr_multicast_if (router alert) succeeded on send_smr %s %s", s1, s2);
	}
#endif

/* multicast loopback */
	retval = pgm_sockaddr_multicast_loop (transport->recv_sock, pgm_sockaddr_family(&transport->recv_smr[0].smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = pgm_sockaddr_multicast_loop (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = pgm_sockaddr_multicast_loop (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
	retval = pgm_sockaddr_multicast_hops (transport->recv_sock, pgm_sockaddr_family(&transport->recv_smr[0].smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = pgm_sockaddr_multicast_hops (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* set low packet latency preference for network elements */
	int tos = IPTOS_LOWDELAY;
	retval = pgm_sockaddr_tos (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), tos);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = pgm_sockaddr_tos (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), tos);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* rx to timer pipe */
	transport->rdata_channel = g_io_channel_unix_new (transport->rdata_pipe[0]);
	g_io_add_watch_context_full (transport->rdata_channel, transport->timer_context, G_PRIORITY_HIGH, G_IO_IN, on_nak_pipe, transport, NULL);

/* tx to timer pipe */
	transport->timer_channel = g_io_channel_unix_new (transport->timer_pipe[0]);
	g_io_add_watch_context_full (transport->timer_channel, transport->timer_context, G_PRIORITY_HIGH, G_IO_IN, on_timer_pipe, transport, NULL);

/* create recyclable SPM packet */
	switch (pgm_sockaddr_family(&transport->recv_smr[0].smr_interface)) {
	case AF_INET:
		transport->spm_len = sizeof(struct pgm_header) + sizeof(struct pgm_spm);
		break;

	case AF_INET6:
		transport->spm_len = sizeof(struct pgm_header) + sizeof(struct pgm_spm6);
		break;
	}
	transport->spm_packet = g_slice_alloc0 (transport->spm_len);

	struct pgm_header* header = (struct pgm_header*)transport->spm_packet;
	struct pgm_spm* spm = (struct pgm_spm*)( header + 1 );
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
	header->pgm_type	= PGM_SPM;

	pgm_sockaddr_to_nla ((struct sockaddr*)&transport->recv_smr[0].smr_interface, (char*)&spm->spm_nla_afi);

/* determine IP header size for rate regulation engine & stats */
	switch (pgm_sockaddr_family(&transport->send_smr.smr_interface)) {
	case AF_INET:
		transport->iphdr_len = sizeof(struct iphdr);
		break;

	case AF_INET6:
		transport->iphdr_len = 40;	/* sizeof(struct ipv6hdr) */
		break;
	}
	g_trace ("INFO","assuming IP header size of %i bytes", transport->iphdr_len);

	if (transport->udp_encap_port)
	{
		guint udphdr_len = sizeof( struct udphdr );
		g_trace ("INFO","assuming UDP header size of %i bytes", udphdr_len);
		transport->iphdr_len += udphdr_len;
	}

/* setup rate control */
	if (transport->txw_max_rte)
	{
		g_trace ("INFO","Setting rate regulation to %i bytes per second.",
				transport->txw_max_rte);
	
		retval = pgm_rate_create (&transport->rate_control, transport->txw_max_rte, transport->iphdr_len);
		if (retval < 0) {
			retval = errno;
			goto out;
		}
	}

	g_trace ("INFO","adding dynamic timer");
	transport->next_poll = transport->next_ambient_spm = pgm_time_update_now() + transport->spm_ambient_interval;
	pgm_add_timer (transport);

/* announce new transport by sending out SPMs */
	send_spm_unlocked (transport);
	send_spm_unlocked (transport);
	send_spm_unlocked (transport);

/* allocate first incoming packet buffer */
	transport->rx_buffer = g_malloc ( transport->max_tpdu );

/* scatter/gather vector for contiguous reading from the window */
	transport->piov_len = IOV_MAX;
	transport->piov = g_malloc ( transport->piov_len * sizeof( struct iovec ) );

/* receiver trash */
	g_static_mutex_init (&transport->rx_mutex);

/* cleanup */
	transport->bound = TRUE;
	g_static_mutex_unlock (&transport->send_mutex);
	g_static_mutex_unlock (&transport->mutex);

	g_trace ("INFO","transport successfully created.");
	return retval;

out:
	return retval;
}

static pgm_peer_t*
new_peer (
	pgm_transport_t*	transport,
	pgm_tsi_t*		tsi,
	struct sockaddr*	src_addr,
	int			src_addr_len
	)
{
	pgm_peer_t* peer;

	g_trace ("INFO","new peer, tsi %s, local nla %s", pgm_print_tsi (tsi), inet_ntoa(((struct sockaddr_in*)src_addr)->sin_addr));
	g_message ("new peer, tsi %s, local nla %s", pgm_print_tsi (tsi), inet_ntoa(((struct sockaddr_in*)src_addr)->sin_addr));

	peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->expiry = pgm_time_update_now() + transport->peer_expiry;
	g_static_mutex_init (&peer->mutex);
	peer->transport = transport;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr = INADDR_ANY;
	memcpy (&peer->local_nla, src_addr, src_addr_len);

/* lock on rx window */
	peer->rxw = pgm_rxw_init (transport->max_tpdu - transport->iphdr_len,
				transport->rxw_preallocate,
				transport->rxw_sqns,
				transport->rxw_secs,
				transport->rxw_max_rte,
				&transport->rx_data,
				&transport->rx_packet,
				&transport->rx_mutex);

	peer->spmr_expiry = pgm_time_update_now() + transport->spmr_expiry;

/* statistics */
	peer->min_fail_time = G_MAXINT;
	peer->max_fail_time = G_MININT;

/* prod timer thread if sleeping */
	g_static_mutex_lock (&transport->mutex);
	if (pgm_time_after( transport->next_poll, peer->spmr_expiry ))
	{
		transport->next_poll = peer->spmr_expiry;
		g_trace ("INFO","new_peer: prod timer thread");
		const char one = '1';
		if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
			/* retval = -EINVAL; */
		}
	}
	g_static_mutex_unlock (&transport->mutex);

/* add peer to hash table and linked list */
	g_static_rw_lock_writer_lock (&transport->peers_lock);
	gpointer entry = pgm_peer_ref(peer);
	g_hash_table_insert (transport->peers_hashtable, &peer->tsi, entry);
	peer->link_.next = transport->peers_list;
	peer->link_.data = peer;
	transport->peers_list = &peer->link_;
	g_static_rw_lock_writer_unlock (&transport->peers_lock);

	return peer;
}

/* data incoming on receive sockets, can be from a sender or receiver, or simply bogus.
 * for IPv4 we receive the IP header to handle fragmentation, for IPv6 we cannot so no idea :(
 *
 * can be called due to event from incoming socket(s) or timer induced data loss.
 *
 * returns bytes read.
 */

int
pgm_transport_recvmsgv (
	pgm_transport_t*	transport,
	pgm_msgv_t*		pmsg,
	int			msg_len,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	g_trace ("INFO", "pgm_transport_recvmsgv");
	int bytes_read = 0;
	pgm_msgv_t* msg_end = pmsg + msg_len;
	struct iovec* piov = transport->piov;
	struct iovec* iov_end = piov + transport->piov_len;

/* first return any previous blocks to the trash */
	int iovi = 0;
	while (transport->iov_len)
	{
		pgm_rxw_data_unref (&transport->rx_data, &transport->rx_mutex, transport->piov[iovi].iov_base);
		transport->iov_len -= transport->piov[iovi].iov_len;

		iovi++;
	}

/* second, flush any remaining contiguous messages from previous call(s) */
	if (transport->peers_waiting)
	{
		g_static_mutex_lock (&transport->waiting_mutex);
		while (transport->peers_waiting)
		{
			pgm_rxw_t* waiting_rxw = transport->peers_waiting->data;
			bytes_read += pgm_rxw_readv (waiting_rxw, &pmsg, msg_end - pmsg, &piov, iov_end - piov);
	
			if (pmsg == msg_end || piov == iov_end)	/* commit full */
			{
				break;
			}

			transport->peers_waiting->data = NULL;
			transport->peers_waiting = transport->peers_waiting->next;
		}
		g_static_mutex_unlock (&transport->waiting_mutex);
	}

/* read the data:
 *
 * Buffer is always max_tpdu in length.  Ideally we have zero copy but the recv includes the ip & pgm headers and
 * the pgm options.  Over thousands of messages the gains by using less receive window memory are more conducive (maybe).
 *
 * We cannot actually block here as packets pushed by the timers need to be addressed too.
 */
	struct sockaddr_storage src_addr;
	socklen_t src_addr_len = sizeof(src_addr);
	int len;
	int bytes_received = 0;

recv_again:
	len = recvfrom (transport->recv_sock, transport->rx_buffer, transport->max_tpdu, MSG_DONTWAIT, (struct sockaddr*)&src_addr, &src_addr_len);
	bytes_received += len;

	if (len < 0) {
		if (bytes_received) {
			goto flush_waiting;
		} else {
			goto out;
		}
	} else if (len == 0) {
		goto out;
	}

#ifdef TRANSPORT_DEBUG
	char s[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop ((struct sockaddr*)&src_addr, s, sizeof(s));
//	g_trace ("INFO","%i bytes received from %s", len, s);
#endif

/* verify IP and PGM header */
	struct sockaddr_storage dst_addr;
	socklen_t dst_addr_len = sizeof(dst_addr);
	struct pgm_header *pgm_header;
	char *packet;
	int packet_len;
	int e;

	if (transport->udp_encap_port) {
		e = pgm_parse_udp_encap(transport->rx_buffer, len, (struct sockaddr*)&dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_len);
	} else {
		e = pgm_parse_raw(transport->rx_buffer, len, (struct sockaddr*)&dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_len);
	}

	if (e < 0)
	{
/* TODO: difference between PGM_PC_SOURCE_CKSUM_ERRORS & PGM_PC_RECEIVER_CKSUM_ERRORS */
		if (e == -2)
			transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto check_for_repeat;
	}

/* calculate senders TSI */
	pgm_tsi_t tsi;
	memcpy (&tsi.gsi, pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	tsi.sport = pgm_header->pgm_sport;

//	g_trace ("INFO","tsi %s", pgm_print_tsi (&tsi));
	pgm_peer_t* source = NULL;

	if (pgm_is_upstream (pgm_header->pgm_type) || pgm_is_peer (pgm_header->pgm_type))
	{

/* upstream = receiver to source, peer-to-peer = receive to receiver
 *
 * NB: SPMRs can be upstream or peer-to-peer, if the packet is multicast then its
 *     a peer-to-peer message, if its unicast its an upstream message.
 */

		if (pgm_header->pgm_sport != transport->dport)
		{

/* its upstream/peer-to-peer for another session */

			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

		if ( pgm_is_peer (pgm_header->pgm_type)
			&& pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst_addr) )
		{

/* its a multicast peer-to-peer message */

			if ( pgm_header->pgm_dport == transport->tsi.sport )
			{

/* we are the source, propagate null as the source */

				source = NULL;
			}
			else
			{
/* we are not the source */

/* check to see the source this peer-to-peer message is about is in our peer list */

				pgm_tsi_t source_tsi;
				memcpy (&source_tsi.gsi, &tsi.gsi, sizeof(pgm_gsi_t));
				source_tsi.sport = pgm_header->pgm_dport;

				g_static_rw_lock_reader_lock (&transport->peers_lock);
				source = g_hash_table_lookup (transport->peers_hashtable, &source_tsi);
				g_static_rw_lock_reader_unlock (&transport->peers_lock);
				if (source == NULL)
				{

/* this source is unknown, we don't care about messages about it */

					goto check_for_repeat;
				}
			}
		}
		else if ( pgm_is_upstream (pgm_header->pgm_type)
			&& !pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst_addr)
			&& ( pgm_header->pgm_dport == transport->tsi.sport ) )
		{

/* unicast upstream message, note that dport & sport are reversed */

			source = NULL;
		}
		else
		{

/* it is a mystery! */

			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

		char *pgm_data = (char*)(pgm_header + 1);
		int pgm_len = packet_len - sizeof(pgm_header);

		switch (pgm_header->pgm_type) {
		case PGM_NAK:
			if (source) {
				on_peer_nak (source, pgm_header, pgm_data, pgm_len);
			} else if (!pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst_addr)) {
				on_nak (transport, &tsi, pgm_header, pgm_data, pgm_len);
				goto check_for_repeat;
			} else {
/* ignore multicast NAKs as the source */
				transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
				goto check_for_repeat;
			}
			break;

		case PGM_NNAK:	on_nnak (transport, &tsi, pgm_header, pgm_data, pgm_len); break;
		case PGM_SPMR:	on_spmr (transport, source, pgm_header, pgm_data, pgm_len); break;
		case PGM_POLR:
		default:
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}
	}
	else
	{

/* downstream = source to receivers */

		if (!pgm_is_downstream (pgm_header->pgm_type))
		{
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

/* pgm packet DPORT contains our transport DPORT */
		if (pgm_header->pgm_dport != transport->dport) {
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

/* search for TSI peer context or create a new one */
		g_static_rw_lock_reader_lock (&transport->peers_lock);
		source = g_hash_table_lookup (transport->peers_hashtable, &tsi);
		g_static_rw_lock_reader_unlock (&transport->peers_lock);
		if (source == NULL)
		{
			source = new_peer (transport, &tsi, (struct sockaddr*)&src_addr, src_addr_len);
		}

		source->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED] += len;
		source->last_packet = pgm_time_now;

		char *pgm_data = (char*)(pgm_header + 1);
		int pgm_len = packet_len - sizeof(pgm_header);

/* handle PGM packet type */
		switch (pgm_header->pgm_type) {
		case PGM_ODATA:	on_odata (source, pgm_header, pgm_data, pgm_len); break;
		case PGM_NCF:	on_ncf (source, pgm_header, pgm_data, pgm_len); break;
		case PGM_RDATA: on_rdata (source, pgm_header, pgm_data, pgm_len); break;
		case PGM_SPM:
			on_spm (source, pgm_header, pgm_data, pgm_len);

/* update group NLA if appropriate */
			if (pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst_addr)) {
				memcpy (&source->group_nla, &dst_addr, dst_addr_len);
			}
			break;
		default:
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

	} /* downstream message */

/* check whether source has waiting data */
	if (source && ((pgm_rxw_t*)source->rxw)->waiting && !((pgm_rxw_t*)source->rxw)->waiting_link.data)
	{
		g_static_mutex_lock (&transport->waiting_mutex);
		((pgm_rxw_t*)source->rxw)->waiting_link.data = source->rxw;
		((pgm_rxw_t*)source->rxw)->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &((pgm_rxw_t*)source->rxw)->waiting_link;
		goto flush_locked;	/* :D */
	}

	if (transport->peers_waiting)
	{

flush_waiting:
/* flush any congtiguous packets generated by the receipt of this packet */
		g_static_mutex_lock (&transport->waiting_mutex);
flush_locked:
		while (transport->peers_waiting)
		{
			pgm_rxw_t* waiting_rxw = transport->peers_waiting->data;
			bytes_read += pgm_rxw_readv (waiting_rxw, &pmsg, msg_end - pmsg, &piov, iov_end - piov);

			if (pmsg == msg_end || piov == iov_end)
			{
				break;
			}

			transport->peers_waiting->data = NULL;
			transport->peers_waiting = transport->peers_waiting->next;
		}
		g_static_mutex_unlock (&transport->waiting_mutex);
	}

check_for_repeat:
/* repeat if non-blocking and not full */
	if (flags & MSG_DONTWAIT)
	{
		if (len > 0 && pmsg < msg_end)
		{
			goto recv_again;		/* \:D/ */
		}
	}
	else
	{
/* repeat if blocking and empty, i.e. received non data packet.
 */
		if (bytes_read == 0)
		{
			int fds = 0;
			fd_set readfds;
			FD_ZERO(&readfds);
			pgm_transport_select_info (transport, &readfds, &fds);
			fds = select (fds, &readfds, NULL, NULL, NULL);
			if (fds == -1) {
				return fds;
			}
			goto recv_again;
		}
	}

out:
	transport->iov_len = bytes_read;
	if (transport->iov_len == 0) {
		errno = EAGAIN;
		return -1;
	}

	return transport->iov_len;
}

int
pgm_transport_recvmsg (
	pgm_transport_t*	transport,
	pgm_msgv_t*		msgv,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	return pgm_transport_recvmsgv (transport, msgv, 1, flags);
}

int
pgm_transport_recv (
	pgm_transport_t*	transport,
	gpointer		data,
	int			len,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	pgm_msgv_t msgv;

	int bytes_read = pgm_transport_recvmsg (transport, &msgv, flags);

/* merge apdu packets together */
	if (bytes_read > 0) {
		int bytes_copied = 0;
		struct iovec* p = msgv.msgv_iov;

		do {
			int src_bytes = msgv.msgv_iov->iov_len;

			if (bytes_copied + src_bytes > len) {
				src_bytes = len - bytes_copied;
				memcpy (data, p->iov_base, src_bytes);
				break;
			}

			memcpy (data, p->iov_base, src_bytes);

			data += src_bytes;
			p++;
			bytes_copied += src_bytes;

		} while (bytes_copied < bytes_read);
	}

	return bytes_read;
}

/* add select parameters for the transports receive socket(s)
 */

int
pgm_transport_select_info (
	pgm_transport_t*	transport,
	fd_set*			readfds,
	int*			n_fds
	)
{
	FD_SET(transport->recv_sock, readfds);
	FD_SET(transport->waiting_pipe[0], readfds);

	return *n_fds = MAX(transport->recv_sock, transport->waiting_pipe[0]) + 1;
}

/* add poll parameters for this transports receive socket(s)
 */

int
pgm_transport_poll_info (
	pgm_transport_t*	transport,
	struct pollfd*		fds,
	int*			n_fds		/* in: #fds, out: used #fds */
	)
{
	g_assert (n_fds > 2);

/* we currently only support one incoming socket */
	fds[0].fd = transport->recv_sock;
	fds[0].events = POLLIN;

	fds[1].fd = transport->waiting_pipe[0];
	fds[1].events = POLLIN;

	return *n_fds = 2;
}

/* add epoll parameters for this transports recieve socket(s)
 */

int
pgm_transport_epoll_ctl (
	pgm_transport_t*	transport,
	int			epfd,
	int			op
	)
{
	int retval = 0;

	if (op != EPOLL_CTL_ADD) {	/* only add currently supported */
		errno = EINVAL;
		retval = -1;
		goto out;
	}

	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = transport;
	retval = epoll_ctl (epfd, op, transport->recv_sock, &event);
	if (retval) {
		goto out;
	}

	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = transport;
	retval = epoll_ctl (epfd, op, transport->waiting_pipe[0], &event);

out:
	return retval;
}

static gboolean
on_io_error (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	g_trace ("INFO","on_io_error()");

	GError *err;
	g_io_channel_shutdown (source, FALSE, &err);

/* TODO: no doubt do something clever here */

/* remove event */
	return FALSE;
}


/* a deferred request for RDATA, now processing in the timer thread, we check the transmit
 * window to see if the packet exists and forward on, maintaining a lock until the queue is
 * empty.
 */

static gboolean
on_nak_pipe (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	pgm_transport_t* transport = data;

/* remove one event from pipe */
	char buf;
	read (transport->rdata_pipe[0], &buf, sizeof(buf));

/* We can flush queue and block all odata, or process one set, or process each
 * sequence number individually.
 */
	pgm_sqn_list_t* sqn_list = g_async_queue_try_pop (transport->rdata_queue);
	if (sqn_list) {
		g_static_rw_lock_reader_lock (&transport->txw_lock);
		for (int i = 0; i < sqn_list->len; i++)
		{
			gpointer rdata = NULL;
			guint rlen = 0;
			if (!pgm_txw_peek (transport->txw, sqn_list->sqn[i], &rdata, &rlen))
			{
				send_rdata (transport, sqn_list->sqn[i], rdata, rlen);
			}
		}
		g_static_rw_lock_reader_unlock (&transport->txw_lock);

		g_static_mutex_lock (&transport->rdata_mutex);
		g_trash_stack_push (&transport->trash_rdata, sqn_list);
		g_static_mutex_unlock (&transport->rdata_mutex);
	}

	return TRUE;
}

/* prod to wakeup timer thread
 */

static gboolean
on_timer_pipe (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	pgm_transport_t* transport = data;

/* empty pipe */
	char buf;
	while (1 == read (transport->timer_pipe[0], &buf, sizeof(buf)));

	return TRUE;
}

/* SPM indicate start of a session, continued presence of a session, or flushing final packets
 * of a session.
 *
 * returns -EINVAL on invalid packet or duplicate SPM sequence number.
 */

static int
on_spm (
	pgm_peer_t*		sender,
	struct pgm_header*	header,
	char*			data,		/* data will be changed to host order on demand */
	int			len
	)
{
	int retval;

	if ((retval = pgm_verify_spm (header, data, len)) != 0)
	{
		sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_transport_t* transport = sender->transport;
	struct pgm_spm* spm = (struct pgm_spm*)data;
	pgm_time_t now = pgm_time_update_now ();

	spm->spm_sqn = g_ntohl (spm->spm_sqn);

/* check for advancing sequence number, or first SPM */
	g_static_mutex_lock (&sender->mutex);
	if ( pgm_uint32_gte (spm->spm_sqn, sender->spm_sqn)
		|| ( ((struct sockaddr*)&sender->nla)->sa_family == 0 ) )
	{
/* copy NLA for replies */
		pgm_nla_to_sockaddr ((const char*)&spm->spm_nla_afi, (struct sockaddr*)&sender->nla);

/* save sequence number */
		sender->spm_sqn = spm->spm_sqn;

/* update receive window */
		pgm_time_t nak_rb_expiry = now + nak_rb_ivl(transport);
		guint naks = pgm_rxw_window_update (sender->rxw,
							g_ntohl (spm->spm_trail),
							g_ntohl (spm->spm_lead),
							nak_rb_expiry);
		g_static_mutex_lock (&transport->mutex);
		if (naks && pgm_time_after(transport->next_poll, nak_rb_expiry))
		{
			transport->next_poll = nak_rb_expiry;
			g_trace ("INFO","on_spm: prod timer thread");
			const char one = '1';
			if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
				g_critical ("write to pipe failed :(");
				retval = -EINVAL;
			}
		}
		g_static_mutex_unlock (&transport->mutex);
	}
	else
	{	/* does not advance SPM sequence number */
		sender->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		retval = -EINVAL;
	}

/* either way bump expiration timer */
	sender->expiry = now + transport->peer_expiry;
	sender->spmr_expiry = 0;
	g_static_mutex_unlock (&sender->mutex);

out:
	return retval;
}

/* SPMR indicates if multicast to cancel own SPMR, or unicast to send SPM.
 *
 * rate limited to 1/IHB_MIN per TSI (13.4).
 */

static int
on_spmr (
	pgm_transport_t*	transport,
	pgm_peer_t*		peer,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	g_trace ("INFO","on_spmr()");

	int retval;

	if ((retval = pgm_verify_spmr (header, data, len)) == 0)
	{

/* we are the source */
		if (peer == NULL)
		{
			send_spm (transport);
		}
		else
		{
/* we are a peer */
			g_trace ("INFO", "suppressing SPMR due to peer multicast SPMR.");
			g_static_mutex_lock (&peer->mutex);
			peer->spmr_expiry = 0;
			g_static_mutex_unlock (&peer->mutex);
		}
	}
	else
	{
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	}

out:
	return retval;
}

/* NAK requesting RDATA transmission for a sending transport, only valid if
 * sequence number(s) still in transmission window.
 *
 * we can potentially have different IP versions for the NAK packet to the send group.
 *
 * TODO: fix IPv6 AFIs
 *
 * take in a NAK and pass off to an asynchronous queue for another thread to process
 */

static int
on_nak (
	pgm_transport_t*	transport,
	pgm_tsi_t*		tsi,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	g_trace ("INFO","on_nak()");
	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED]++;

	int retval;
	if ((retval = pgm_verify_nak (header, data, len)) != 0)
	{
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_nak* nak = (struct pgm_nak*)data;
		
/* NAK_SRC_NLA contains our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr ((const char*)&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_smr.smr_interface) != 0) {
		retval = -EINVAL;
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

/* NAK_GRP_NLA containers our transport multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	switch (pgm_sockaddr_family(&nak_src_nla)) {
	case AF_INET:
		pgm_nla_to_sockaddr ((const char*)&nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
		break;

	case AF_INET6:
		pgm_nla_to_sockaddr ((const char*)&((struct pgm_nak6*)nak)->nak6_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
		break;
	}

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->send_smr.smr_multiaddr) != 0) {
		retval = -EINVAL;
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

/* create queue object */
	g_static_mutex_lock (&transport->rdata_mutex);
	pgm_sqn_list_t* sqn_list = pgm_sqn_list_alloc (transport);
	g_static_mutex_unlock (&transport->rdata_mutex);
	sqn_list->sqn[0] = g_ntohl (nak->nak_sqn);
	sqn_list->len++;

	g_trace ("INFO", "nak_sqn %" G_GUINT32_FORMAT, sqn_list->sqn[0]);

/* check NAK list */
	guint32* nak_list = NULL;
	int nak_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(nak + 1);
		if (opt_header->opt_type != PGM_OPT_LENGTH)
		{
			retval = -EINVAL;
			transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto out;
		}
		if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length))
		{
			retval = -EINVAL;
			transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto out;
		}
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
/* TODO: check for > 16 options & past packet end */
		do {
			opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				nak_list = ((struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				nak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint16) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	gpointer rdata = NULL;
	int rlen = 0;

/* nak list numbers */
#ifdef TRANSPORT_DEBUG
	if (nak_list)
	{
		char nak_sz[1024] = "";
		guint32 *nakp = nak_list, *nake = nak_list + nak_list_len;
		while (nakp < nake) {
			char tmp[1024];
			sprintf (tmp, "%" G_GUINT32_FORMAT " ", *nakp);
			strcat (nak_sz, tmp);
			nakp++;
		}
	g_trace ("INFO", "nak list %s", nak_sz);
	}
#endif
	for (int i = 0; i < nak_list_len; i++)
	{
		sqn_list->sqn[sqn_list->len++] = g_ntohl (*nak_list);
		nak_list++;
	}

/* send NAK confirm packet immediately, then defer to timer thread for a.s.a.p
 * delivery of the actual RDATA packets.
 */
	if (nak_list_len) {
		send_ncf_list (transport, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, sqn_list);
	} else {
		send_ncf (transport, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, sqn_list->sqn[0]);
	}

	g_async_queue_lock (transport->rdata_queue);
	g_async_queue_push_unlocked (transport->rdata_queue, sqn_list);

	{
		const char one = '1';
		if (1 != write (transport->rdata_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
			retval = -EINVAL;
		}
	}
	g_async_queue_unlock (transport->rdata_queue);

out:
	return retval;
}

/* Multicast peer-to-peer NAK handling, pretty much the same as a NCF but different direction
 */

static int
on_peer_nak (
	pgm_peer_t*		peer,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	g_trace ("INFO","on_peer_nak()");

	int retval;
	pgm_transport_t* transport = peer->transport;

	if ((retval = pgm_verify_nak (header, data, len)) != 0)
	{
		g_trace ("INFO", "Invalid NAK, ignoring.");
		peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS]++;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_nak* nak = (struct pgm_nak*)data;
		
/* NAK_SRC_NLA must not contain our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr ((const char*)&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_smr.smr_interface) == 0) {
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

/* NAK_GRP_NLA contains one of our transport receive multicast groups: the sources send multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	switch (pgm_sockaddr_family(&nak_src_nla)) {
	case AF_INET:
		pgm_nla_to_sockaddr ((const char*)&nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
		break;

	case AF_INET6:
		pgm_nla_to_sockaddr ((const char*)&((struct pgm_nak6*)nak)->nak6_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
		break;
	}

	gboolean found = FALSE;
	for (int i = 0; i < transport->recv_smr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->recv_smr[i].smr_multiaddr) == 0)
		{
			found = TRUE;
		}
	}

	if (!found) {
		g_trace ("INFO", "NAK not destined for this multicast group.");
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	g_static_mutex_lock (&transport->mutex);
	g_static_mutex_lock (&peer->mutex);

/* handle as NCF */
	pgm_time_update_now();
	pgm_rxw_ncf (peer->rxw, g_ntohl (nak->nak_sqn), pgm_time_now + transport->nak_rdata_ivl, pgm_time_now + nak_rb_ivl(transport));

/* check NAK list */
	guint32* nak_list = NULL;
	int nak_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(nak + 1);
		if (opt_header->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO", "First PGM Option in NAK incorrect, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO", "PGM Length Option has incorrect length, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
/* TODO: check for > 16 options & past packet end */
		do {
			opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				nak_list = ((struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				nak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint16) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	g_trace ("INFO", "NAK contains 1+%i sequence numbers.", nak_list_len);
	while (nak_list_len)
	{
		pgm_rxw_ncf (peer->rxw, g_ntohl (*nak_list), pgm_time_now + transport->nak_rdata_ivl, pgm_time_now + nak_rb_ivl(transport));
		nak_list++;
		nak_list_len--;
	}

out_unlock:
	g_static_mutex_unlock (&peer->mutex);
	g_static_mutex_unlock (&transport->mutex);

out:
	return retval;
}

/* NCF confirming receipt of a NAK from this transport or another on the LAN segment.
 *
 * Packet contents will match exactly the sent NAK, although not really that helpful.
 */

static int
on_ncf (
	pgm_peer_t*		peer,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	g_trace ("INFO","on_ncf()");

	int retval;
	pgm_transport_t* transport = peer->transport;

	if ((retval = pgm_verify_ncf (header, data, len)) != 0)
	{
		g_trace ("INFO", "Invalid NCF, ignoring.");
		peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_nak* ncf = (struct pgm_nak*)data;
		
/* NCF_SRC_NLA may contain our transport unicast NLA, we don't really care */
	struct sockaddr_storage ncf_src_nla;
	pgm_nla_to_sockaddr ((const char*)&ncf->nak_src_nla_afi, (struct sockaddr*)&ncf_src_nla);

#if 0
	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_src_nla, (struct sockaddr*)&transport->send_smr.smr_interface) != 0) {
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}
#endif

/* NCF_GRP_NLA contains our transport multicast group */ 
	struct sockaddr_storage ncf_grp_nla;
	switch (pgm_sockaddr_family(&ncf_src_nla)) {
	case AF_INET:
		pgm_nla_to_sockaddr ((const char*)&ncf->nak_grp_nla_afi, (struct sockaddr*)&ncf_grp_nla);
		break;

	case AF_INET6:
		pgm_nla_to_sockaddr ((const char*)&((struct pgm_nak6*)ncf)->nak6_grp_nla_afi, (struct sockaddr*)&ncf_grp_nla);
		break;
	}

	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_grp_nla, (struct sockaddr*)&transport->send_smr.smr_multiaddr) != 0) {
		g_trace ("INFO", "NCF not destined for this multicast group.");
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	g_static_mutex_lock (&transport->mutex);
	g_static_mutex_lock (&peer->mutex);

	pgm_time_update_now();
	pgm_rxw_ncf (peer->rxw, g_ntohl (ncf->nak_sqn), pgm_time_now + transport->nak_rdata_ivl, pgm_time_now + nak_rb_ivl(transport));

/* check NCF list */
	guint32* ncf_list = NULL;
	int ncf_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(ncf + 1);
		if (opt_header->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO", "First PGM Option in NCF incorrect, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO", "PGM Length Option has incorrect length, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
/* TODO: check for > 16 options & past packet end */
		do {
			opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				ncf_list = ((struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				ncf_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint16) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	g_trace ("INFO", "NCF contains 1+%i sequence numbers.", ncf_list_len);
	while (ncf_list_len)
	{
		pgm_rxw_ncf (peer->rxw, g_ntohl (*ncf_list), pgm_time_now + transport->nak_rdata_ivl, pgm_time_now + nak_rb_ivl(transport));
		ncf_list++;
		ncf_list_len--;
	}

out_unlock:
	g_static_mutex_unlock (&peer->mutex);
	g_static_mutex_unlock (&transport->mutex);

out:
	return retval;
}

/* Null-NAK, or N-NAK propogated by a DLR for hand waving excitement
 */

static int
on_nnak (
	pgm_transport_t*	transport,
	pgm_tsi_t*		tsi,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	g_trace ("INFO","on_nnak()");
	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED]++;

	int retval;
	if ((retval = pgm_verify_nnak (header, data, len)) != 0)
	{
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_nak* nnak = (struct pgm_nak*)data;
		
/* NAK_SRC_NLA contains our transport unicast NLA */
	struct sockaddr_storage nnak_src_nla;
	pgm_nla_to_sockaddr ((const char*)&nnak->nak_src_nla_afi, (struct sockaddr*)&nnak_src_nla);

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nnak_src_nla, (struct sockaddr*)&transport->send_smr.smr_interface) != 0) {
		retval = -EINVAL;
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

/* NAK_GRP_NLA containers our transport multicast group */ 
	struct sockaddr_storage nnak_grp_nla;
	switch (pgm_sockaddr_family(&nnak_src_nla)) {
	case AF_INET:
		pgm_nla_to_sockaddr ((const char*)&nnak->nak_grp_nla_afi, (struct sockaddr*)&nnak_grp_nla);
		break;

	case AF_INET6:
		pgm_nla_to_sockaddr ((const char*)&((struct pgm_nak6*)nnak)->nak6_grp_nla_afi, (struct sockaddr*)&nnak_grp_nla);
		break;
	}

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nnak_grp_nla, (struct sockaddr*)&transport->send_smr.smr_multiaddr) != 0) {
		retval = -EINVAL;
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto out;
	}

/* check NNAK list */
	guint nnak_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(nnak + 1);
		if (opt_header->opt_type != PGM_OPT_LENGTH)
		{
			retval = -EINVAL;
			transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto out;
		}
		if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length))
		{
			retval = -EINVAL;
			transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto out;
		}
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
/* TODO: check for > 16 options & past packet end */
		do {
			opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				nnak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint16) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED] += 1 + nnak_list_len;

out:
	return retval;
}

/* ambient/heartbeat SPM's
 *
 * heartbeat: ihb_tmr decaying between ihb_min and ihb_max 2x after last packet
 */

static inline int
send_spm (
	pgm_transport_t*	transport
	)
{
	g_static_mutex_lock (&transport->mutex);
	int retval = send_spm_unlocked (transport);
	g_static_mutex_unlock (&transport->mutex);
	return retval;
}

static int
send_spm_unlocked (
	pgm_transport_t*	transport
	)
{
	int retval = 0;
	g_trace ("SPM","send_spm");

/* recycles a transport global packet */
	struct pgm_header *header = (struct pgm_header*)transport->spm_packet;
	struct pgm_spm *spm = (struct pgm_spm*)(header + 1);

	spm->spm_sqn		= g_htonl (transport->spm_sqn++);
	g_static_rw_lock_reader_lock (&transport->txw_lock);
	spm->spm_trail		= g_htonl (pgm_txw_trail(transport->txw));
	spm->spm_lead		= g_htonl (pgm_txw_lead(transport->txw));
	g_static_rw_lock_reader_unlock (&transport->txw_lock);

/* checksum optional for SPMs */
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_checksum((char*)header, transport->spm_len, 0);

	retval = pgm_sendto (transport,
				TRUE,				/* with router alert */
				header,
				transport->spm_len,
				MSG_CONFIRM,			/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += transport->spm_len;

	return retval;
}

/* send SPM-request to a new peer, this packet type has no contents
 */

static int
send_spmr (
	pgm_peer_t*	peer
	)
{
	g_trace ("INFO","send_spmr");

	int retval = 0;
	pgm_transport_t* transport = peer->transport;

/* cache peer information */
	guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->local_nla, sizeof(struct sockaddr_storage));

	int tpdu_length = sizeof(struct pgm_header);
	char buf[ tpdu_length ];
	struct pgm_header *header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
/* dport & sport reversed communicating upstream */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type	= PGM_SPMR;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_mutex);

/* send multicast SPMR TTL 1 */
	g_trace ("INFO", "send multicast SPMR to %s", inet_ntoa( ((struct sockaddr_in*)&transport->send_smr.smr_multiaddr)->sin_addr ));
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), 1);
	retval = sendto (transport->send_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));

/* send unicast SPMR with regular TTL */
	g_trace ("INFO", "send unicast SPMR to %s", inet_ntoa( ((struct sockaddr_in*)&peer->local_nla)->sin_addr ));
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
	retval = sendto (transport->send_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));

	g_static_mutex_unlock (&transport->send_mutex);

	peer->spmr_expiry = 0;

	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length * 2;

out:
	return retval;
}

static int
send_nak (
	pgm_peer_t*		peer,
	guint32			sequence_number
	)
{
	g_trace ("INFO", "send_nak(%" G_GUINT32_FORMAT ")", sequence_number);

	int retval = 0;
	pgm_transport_t* transport = peer->transport;
	gchar buf[ sizeof(struct pgm_header) + sizeof(struct pgm_nak) ];
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));

	guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sequence_number);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer->group_nla, (char*)&nak->nak_grp_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
	retval = sendto (transport->send_with_router_alert_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

//	g_trace ("INFO","%i bytes sent", tpdu_length);
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT]++;

out:
	return retval;
}

/* send a NAK confirm (NCF) message with provided sequence number list.
 */

static int
send_ncf (
	pgm_transport_t*	transport,
	struct sockaddr*	nak_src_nla,
	struct sockaddr*	nak_grp_nla,
	guint32			sequence_number
	)
{
	g_trace ("INFO", "send_ncf()");

	int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	gchar buf[ tpdu_length ];

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *ncf = (struct pgm_nak*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
	header->pgm_type        = PGM_NCF;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NCF */
	ncf->nak_sqn		= g_htonl (sequence_number);

/* source nla */
	pgm_sockaddr_to_nla (nak_src_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla (nak_grp_nla, (char*)&ncf->nak_grp_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
	retval = sendto (transport->send_with_router_alert_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length;
//	g_trace ("INFO","%i bytes sent", tpdu_length);

out:
	return retval;
}

/* A NAK packet with a OPT_NAK_LIST option extension
 */

#ifndef PGM_SINGLE_NAK
static int
send_nak_list (
	pgm_peer_t*	peer,
	pgm_sqn_list_t*	sqn_list
	)
{
	g_assert (sqn_list->len > 1);
	g_assert (sqn_list->len <= 63);

	int retval = 0;
	pgm_transport_t* transport = peer->transport;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak)
			+ sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length)
			+ sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
			+ ( (sqn_list->len-1) * sizeof(guint32) );
	gchar buf[ tpdu_length ];

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));

	guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PRESENT;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer->group_nla, (char*)&nak->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(nak + 1);
	opt_header->opt_type	= PGM_OPT_LENGTH;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length);
	struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
	opt_length->opt_total_length = g_htons (sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( (sqn_list->len-1) * sizeof(guint32) ));
	opt_header = (struct pgm_opt_header*)(opt_length + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
				+ ( (sqn_list->len-1) * sizeof(guint32) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;

#ifdef TRANSPORT_DEBUG
	char nak1[1024];
	sprintf (nak1, "send_nak_list( %" G_GUINT32_FORMAT " + [", sqn_list->sqn[0]);
#endif
	for (int i = 1; i < sqn_list->len; i++) {
		opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);

#ifdef TRANSPORT_DEBUG
		char nak2[1024];
		sprintf (nak2, "%" G_GUINT32_FORMAT " ", sqn_list->sqn[i]);
		strcat (nak1, nak2);
#endif
	}

#ifdef TRANSPORT_DEBUG
	g_trace ("INFO", "%s]%i )", nak1, sqn_list->len);
#endif

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_mutex);
	retval = sendto (transport->send_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));
	g_static_mutex_unlock (&transport->send_mutex);

//	g_trace ("INFO","%i bytes sent", tpdu_length);
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT] += 1 + sqn_list->len;

	return retval;
}

static int
send_ncf_list (
	pgm_transport_t*	transport,
	struct sockaddr*	nak_src_nla,
	struct sockaddr*	nak_grp_nla,
	pgm_sqn_list_t*		sqn_list
	)
{
	g_assert (sqn_list->len > 1);
	g_assert (sqn_list->len <= 63);

	int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak)
			+ sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length)
			+ sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
			+ ( (sqn_list->len-1) * sizeof(guint32) );
	gchar buf[ tpdu_length ];

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *ncf = (struct pgm_nak*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));

	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
	header->pgm_type        = PGM_NCF;
        header->pgm_options     = PGM_OPT_PRESENT;
        header->pgm_tsdu_length = 0;

/* NCF */
	ncf->nak_sqn		= g_htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla (nak_src_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla (nak_grp_nla, (char*)&ncf->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(ncf + 1);
	opt_header->opt_type	= PGM_OPT_LENGTH;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length);
	struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
	opt_length->opt_total_length = g_htons (sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( (sqn_list->len-1) * sizeof(guint32) ));
	opt_header = (struct pgm_opt_header*)(opt_length + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
				+ ( (sqn_list->len-1) * sizeof(guint32) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;

#ifdef TRANSPORT_DEBUG
	char nak1[1024];
	sprintf (nak1, "send_ncf_list( %" G_GUINT32_FORMAT " + [", sqn_list->sqn[0]);
#endif
	for (int i = 1; i < sqn_list->len; i++) {
		opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);

#ifdef TRANSPORT_DEBUG
		char nak2[1024];
		sprintf (nak2, "%" G_GUINT32_FORMAT " ", sqn_list->sqn[i]);
		strcat (nak1, nak2);
#endif
	}

#ifdef TRANSPORT_DEBUG
	g_trace ("INFO", "%s]%i )", nak1, sqn_list->len);
#endif

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
	retval = sendto (transport->send_with_router_alert_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length;
//	g_trace ("INFO","%i bytes sent", tpdu_length);

	return retval;
}
#endif /* !PGM_SINGLE_NAK */

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update transport::next_nak_rb_timestamp for next expiration time.
 *
 * peer object is locked before entry.
 */

static void
nak_rb_state (
	gpointer		tsi,
	gpointer		user_data
	)
{
	pgm_peer_t* peer = (pgm_peer_t*)user_data;
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list;
#ifndef PGM_SINGLE_NAK
	pgm_sqn_list_t nak_list;
	nak_list.len = 0;
#endif

	g_trace ("INFO", "nak_rb_state(len=%u)", g_list_length(rxw->backoff_queue->tail));

/* send all NAKs first, lack of data is blocking contiguous processing and its 
 * better to get the notification out a.s.a.p. even though it might be waiting
 * in a kernel queue.
 *
 * alternative: after each packet check for incoming data and return to the
 * event loop.  bias for shorter loops as retry count increases.
 */
	list = rxw->backoff_queue->tail;
	if (!list) {
		g_warning ("backoff queue is empty in nak_rb_state.");
		return;
	}

	guint dropped_invalid = 0;

/* have not learned this peers NLA */
	gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

	while (list)
	{
		GList* next_list_el = list->prev;
		pgm_rxw_packet_t* rp = (pgm_rxw_packet_t*)list->data;

/* check this packet for state expiration */
		if (pgm_time_after_eq(pgm_time_now, rp->nak_rb_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", rp->sequence_number);
				pgm_rxw_mark_lost (rxw, rp->sequence_number);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					g_static_mutex_lock (&transport->waiting_mutex);
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
					g_static_mutex_unlock (&transport->waiting_mutex);
				}

				list = next_list_el;
				continue;
			}

/* remove from this state */
			pgm_rxw_pkt_state_unlink (rxw, rp);

#if PGM_SINGLE_NAK
			send_nak (transport, peer, rp->sequence_number);
			pgm_time_update_now();
#else
			nak_list.sqn[nak_list.len++] = rp->sequence_number;
#endif

			rp->nak_transmit_count++;

			rp->state = PGM_PKT_WAIT_NCF_STATE;
			g_queue_push_head_link (rxw->wait_ncf_queue, &rp->link_);

/* we have two options here, calculate the expiry time in the new state relative to the current
 * state execution time, skipping missed expirations due to delay in state processing, or base
 * from the actual current time.
 */
#ifdef PGM_ABSOLUTE_EXPIRY
			rp->nak_rpt_expiry = rp->nak_rb_expiry + transport->nak_rpt_ivl;
			while (pgm_time_after_eq(pgm_time_now, rp->nak_rpt_expiry){
				rp->nak_rpt_expiry += transport->nak_rpt_ivl;
				rp->ncf_retry_count++;
			}
#else
			rp->nak_rpt_expiry = pgm_time_now + transport->nak_rpt_ivl;
g_trace("INFO", "rp->nak_rpt_expiry in %f seconds.",
		pgm_to_secsf( rp->nak_rpt_expiry - pgm_time_now ) );
#endif

#ifndef PGM_SINGLE_NAK
			if (nak_list.len == G_N_ELEMENTS(nak_list.sqn)) {
				send_nak_list (peer, &nak_list);
				pgm_time_update_now();
				nak_list.len = 0;
			}
#endif
		}
		else
		{	/* packet expires some time later */
			break;
		}

		list = next_list_el;
	}

#ifndef PGM_SINGLE_NAK
	if (nak_list.len)
	{
		if (nak_list.len > 1) {
			send_nak_list (peer, &nak_list);
		} else {
			g_assert (nak_list.len == 1);
			send_nak (peer, nak_list.sqn[0]);
		}
	}
#endif

	if (dropped_invalid) {
		g_message ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (rxw->backoff_queue->length == 0)
	{
		g_assert ((struct rxw_packet*)rxw->backoff_queue->head == NULL);
		g_assert ((struct rxw_packet*)rxw->backoff_queue->tail == NULL);
	}
	else
	{
		g_assert ((struct rxw_packet*)rxw->backoff_queue->head != NULL);
		g_assert ((struct rxw_packet*)rxw->backoff_queue->tail != NULL);
	}

	if (rxw->backoff_queue->tail)
		g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf((float)next_nak_rb_expiry(rxw) - (float)pgm_time_now));
	else
		g_trace ("INFO", "backoff queue empty.");
}

/* check this peer for NAK state timers, uses the tail of each queue for the nearest
 * timer execution.
 */

static void
check_peer_nak_state (
	pgm_transport_t*	transport
	)
{
	if (!transport->peers_list) {
		return;
	}

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = list->data;
		pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
		pgm_transport_t* transport = peer->transport;

		g_static_mutex_lock (&peer->mutex);

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (pgm_time_now, peer->spmr_expiry))
			{
				send_spmr (peer);
			}
		}

		if (rxw->backoff_queue->tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rb_expiry(rxw)))
			{
				nak_rb_state (&peer->tsi, peer);
			}
		}
		
		if (rxw->wait_ncf_queue->tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rpt_expiry(rxw)))
			{
				nak_rpt_state (&peer->tsi, peer);
			}
		}

		if (rxw->wait_data_queue->tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rdata_expiry(rxw)))
			{
				nak_rdata_state (&peer->tsi, peer);
			}
		}

/* expired, remove from hash table and linked list */
		if (pgm_time_after_eq (pgm_time_now, peer->expiry))
		{
			g_message ("peer expired, tsi %s", pgm_print_tsi (&peer->tsi));
			g_hash_table_remove (transport->peers_hashtable, &peer->tsi);
			transport->peers_list = g_list_remove_link (transport->peers_list, &peer->link_);
			g_static_mutex_unlock (&peer->mutex);
			pgm_peer_unref (peer);
		}
		else
		{
			g_static_mutex_unlock (&peer->mutex);
		}

		list = next;
	} while (list);

/* check for waiting contiguous packets */
	if (transport->peers_waiting) {
		g_trace ("INFO","prod rx thread");
		const char one = '1';
		if (1 != write (transport->waiting_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
		}
	}
}

static pgm_time_t
min_nak_expiry (
	pgm_time_t		expiration,
	pgm_transport_t*	transport
	)
{
	if (!transport->peers_list) {
		goto out;
	}

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = (pgm_peer_t*)list->data;
		pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	
		g_static_mutex_lock (&peer->mutex);

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (expiration, peer->spmr_expiry))
			{
				expiration = peer->spmr_expiry;
			}
		}

		if (rxw->backoff_queue->tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rb_expiry(rxw)))
			{
				expiration = next_nak_rb_expiry(rxw);
			}
		}

		if (rxw->wait_ncf_queue->tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rpt_expiry(rxw)))
			{
				expiration = next_nak_rpt_expiry(rxw);
			}
		}

		if (rxw->wait_data_queue->tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rdata_expiry(rxw)))
			{
				expiration = next_nak_rdata_expiry(rxw);
			}
		}
	
		g_static_mutex_unlock (&peer->mutex);

		list = next;
	} while (list);

out:
	return expiration;
}

/* check WAIT_NCF_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_NCF_RETRIES
 * cancel the sequence number.
 */

static void
nak_rpt_state (
	gpointer		tsi,
	gpointer		user_data
	)
{
	pgm_peer_t* peer = (pgm_peer_t*)user_data;
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list = rxw->wait_ncf_queue->tail;

	g_trace ("INFO", "nak_rpt_state(len=%u)", g_list_length(rxw->wait_ncf_queue->tail));

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

	while (list)
	{
		GList* next_list_el = list->prev;
		pgm_rxw_packet_t* rp = (pgm_rxw_packet_t*)list->data;

/* check this packet for state expiration */
		if (pgm_time_after_eq(pgm_time_now, rp->nak_rpt_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", rp->sequence_number);
				pgm_rxw_mark_lost (rxw, rp->sequence_number);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					g_static_mutex_lock (&transport->waiting_mutex);
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
					g_static_mutex_unlock (&transport->waiting_mutex);
				}

				list = next_list_el;
				continue;
			}

			if (++rp->ncf_retry_count > transport->nak_ncf_retries)
			{
/* cancellation */
				dropped++;
				g_trace ("INFO", "lost data #%u due to cancellation.", rp->sequence_number);

				gint fail_time = pgm_time_now - rp->t0;
				if (fail_time > peer->max_fail_time)		peer->max_fail_time = fail_time;
				else if (fail_time < peer->min_fail_time)	peer->min_fail_time = fail_time;

				pgm_rxw_mark_lost (rxw, rp->sequence_number);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					g_static_mutex_lock (&transport->waiting_mutex);
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
					g_static_mutex_unlock (&transport->waiting_mutex);
				}

				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED]++;
			}
			else
			{
/* retry */
				g_trace("INFO", "retry #%u attempt %u/%u.", rp->sequence_number, rp->ncf_retry_count, transport->nak_ncf_retries);
				pgm_rxw_pkt_state_unlink (rxw, rp);
				rp->state = PGM_PKT_BACK_OFF_STATE;
				g_queue_push_head_link (rxw->backoff_queue, &rp->link_);
//				rp->nak_rb_expiry = rp->nak_rpt_expiry + nak_rb_ivl(transport);
				rp->nak_rb_expiry = pgm_time_now + nak_rb_ivl(transport);
			}
		}
		else
		{
/* packet expires some time later */
			g_trace("INFO", "#%u retry is delayed %f seconds.", rp->sequence_number, pgm_to_secsf(rp->nak_rpt_expiry - pgm_time_now));
			break;
		}
		

		list = next_list_el;
	}

	if (rxw->wait_ncf_queue->length == 0)
	{
		g_assert ((pgm_rxw_packet_t*)rxw->wait_ncf_queue->head == NULL);
		g_assert ((pgm_rxw_packet_t*)rxw->wait_ncf_queue->tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_packet_t*)rxw->wait_ncf_queue->head);
		g_assert ((pgm_rxw_packet_t*)rxw->wait_ncf_queue->tail);
	}

	if (dropped_invalid) {
		g_message ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (dropped) {
		g_message ("dropped %u messages due to ncf cancellation, "
				"rxw_sqns %" G_GUINT32_FORMAT
				" bo %" G_GUINT32_FORMAT
				" ncf %" G_GUINT32_FORMAT
				" wd %" G_GUINT32_FORMAT
				" lost %" G_GUINT32_FORMAT
				" frag %" G_GUINT32_FORMAT,
				dropped,
				pgm_rxw_sqns(rxw),
				rxw->backoff_queue->length,
				rxw->wait_ncf_queue->length,
				rxw->wait_data_queue->length,
				rxw->lost_count,
				rxw->fragment_count);
	}

	if (rxw->wait_ncf_queue->tail) {
		if (next_nak_rpt_expiry(rxw) > pgm_time_now)
		{
			g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rpt_expiry(rxw) - pgm_time_now));
		} else {
			g_trace ("INFO", "next expiry set in -%f seconds.", pgm_to_secsf(pgm_time_now - next_nak_rpt_expiry(rxw)));
		}
	} else
		g_trace ("INFO", "wait ncf queue empty.");
}

/* check WAIT_DATA_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_DATA_RETRIES
 * canel the sequence number.
 */

static void
nak_rdata_state (
	gpointer		tsi,
	gpointer		peer_
	)
{
	pgm_peer_t* peer = (pgm_peer_t*)peer_;
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list = rxw->wait_data_queue->tail;

	g_trace ("INFO", "nak_rdata_state(len=%u)", g_list_length(rxw->wait_data_queue->tail));

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

	while (list)
	{
		GList* next_list_el = list->prev;
		pgm_rxw_packet_t* rp = (pgm_rxw_packet_t*)list->data;

/* check this packet for state expiration */
		if (pgm_time_after_eq(pgm_time_now, rp->nak_rdata_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", rp->sequence_number);
				pgm_rxw_mark_lost (rxw, rp->sequence_number);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					g_static_mutex_lock (&transport->waiting_mutex);
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
					g_static_mutex_unlock (&transport->waiting_mutex);
				}

				list = next_list_el;
				continue;
			}

			if (++rp->data_retry_count > transport->nak_data_retries)
			{
/* cancellation */
				dropped++;
				g_trace ("INFO", "lost data #%u due to cancellation.", rp->sequence_number);

				gint fail_time = pgm_time_now - rp->t0;
				if (fail_time > peer->max_fail_time)		peer->max_fail_time = fail_time;
				else if (fail_time < peer->min_fail_time)	peer->min_fail_time = fail_time;

				pgm_rxw_mark_lost (rxw, rp->sequence_number);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					g_static_mutex_lock (&transport->waiting_mutex);
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
					g_static_mutex_unlock (&transport->waiting_mutex);
				}

				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED]++;

				list = next_list_el;
				continue;
			}

/* remove from this state */
			pgm_rxw_pkt_state_unlink (rxw, rp);

/* retry back to back-off state */
			g_trace("INFO", "retry #%u attempt %u/%u.", rp->sequence_number, rp->data_retry_count, transport->nak_data_retries);
			rp->state = PGM_PKT_BACK_OFF_STATE;
			g_queue_push_head_link (rxw->backoff_queue, &rp->link_);
//			rp->nak_rb_expiry = rp->nak_rdata_expiry + nak_rb_ivl(transport);
			rp->nak_rb_expiry = pgm_time_now + nak_rb_ivl(transport);
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (rxw->wait_data_queue->length == 0)
	{
		g_assert ((pgm_rxw_packet_t*)rxw->wait_data_queue->head == NULL);
		g_assert ((pgm_rxw_packet_t*)rxw->wait_data_queue->tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_packet_t*)rxw->wait_data_queue->head);
		g_assert ((pgm_rxw_packet_t*)rxw->wait_data_queue->tail);
	}

	if (dropped_invalid) {
		g_message ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (dropped) {
		g_message ("dropped %u messages due to data cancellation.", dropped);
	}

	if (rxw->wait_data_queue->tail)
		g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rdata_expiry(rxw) - pgm_time_now));
	else
		g_trace ("INFO", "wait data queue empty.");
}

static int
pgm_reset_heartbeat_spm (pgm_transport_t* transport)
{
	int retval = 0;

	g_static_mutex_lock (&transport->mutex);

/* re-set spm timer */
	transport->spm_heartbeat_state = 1;
	transport->next_heartbeat_spm = pgm_time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];

/* prod timer thread if sleeping */
	if (pgm_time_after( transport->next_poll, transport->next_heartbeat_spm ))
	{
		transport->next_poll = transport->next_heartbeat_spm;
		g_trace ("INFO","pgm_reset_heartbeat_spm: prod timer thread");
		const char one = '1';
		if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
			retval = -EINVAL;
		}
	}

	g_static_mutex_unlock (&transport->mutex);

	return retval;
}

/* can be called from any thread, it needs to update the transmit window with the new
 * data and then send on the wire, only then can control return to the callee.
 *
 * special care is necessary with the provided memory, it must be previously allocated
 * from the transmit window, and offset to include the pgm header.
 */

int
pgm_transport_send_one_unlocked (
	pgm_transport_t*	transport,
	const gchar*		buf,
	gsize			count
	)
{
	int retval = 0;

/* retrieve packet storage from transmit window */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + count;
	char *pkt = (char*)buf - sizeof(struct pgm_header) - sizeof(struct pgm_data);

	struct pgm_header *header = (struct pgm_header*)pkt;
	struct pgm_data *odata = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
	header->pgm_type        = PGM_ODATA;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = g_htons (count);

/* ODATA */
        odata->data_sqn         = g_htonl (pgm_txw_next_lead(transport->txw));
        odata->data_trail       = g_htonl (pgm_txw_trail(transport->txw));

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

/* add to transmit window */
	pgm_txw_push (transport->txw, pkt, tpdu_length);

	retval = pgm_sendto (transport,
				FALSE,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));

/* release txw lock here in order to allow spms to lock mutex */
	g_static_rw_lock_writer_unlock (&transport->txw_lock);

	pgm_reset_heartbeat_spm (transport);

	transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += count;
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]++;
	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length + transport->iphdr_len;
//	g_trace ("INFO","%i bytes sent", tpdu_length);

	return retval;
}

/* copy application data (apdu) to multiple tx window (tpdu) entries and send.
 *
 * TODO: generic wrapper to determine fragmentation from tpdu_length.
 */

int
pgm_transport_send_fragment_unlocked (
	pgm_transport_t*	transport,
	const gchar*		buf,
	gsize			count)
{
	int retval = 0;
	guint offset = 0;
	guint32 opt_sqn = pgm_txw_next_lead(transport->txw);
	guint packets = 0;
	guint bytes_sent = 0;

	do {
/* retrieve packet storage from transmit window */
		int header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + 
				sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length) +
				sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment);
		int tsdu_length = MIN(transport->max_tpdu - transport->iphdr_len - header_length, count - offset);
		int tpdu_length = header_length + tsdu_length;

		char *pkt = pgm_txw_alloc(transport->txw);
		struct pgm_header *header = (struct pgm_header*)pkt;
		memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_tsi_t));
		header->pgm_sport	= transport->tsi.sport;
		header->pgm_dport	= transport->dport;
		header->pgm_type        = PGM_ODATA;
	        header->pgm_options     = PGM_OPT_PRESENT;
	        header->pgm_tsdu_length = g_htons (tsdu_length);

/* ODATA */
		struct pgm_data *odata = (struct pgm_data*)(header + 1);
	        odata->data_sqn         = g_htonl (pgm_txw_next_lead(transport->txw));
	        odata->data_trail       = g_htonl (pgm_txw_trail(transport->txw));

/* OPT_LENGTH */
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(odata + 1);
		opt_header->opt_type	= PGM_OPT_LENGTH;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_length);
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
		opt_length->opt_total_length	= g_htons (sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_fragment));

/* OPT_FRAGMENT */
		opt_header = (struct pgm_opt_header*)(opt_length + 1);
		opt_header->opt_type	= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_fragment);
		struct pgm_opt_fragment* opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
		opt_fragment->opt_reserved	= 0;
		opt_fragment->opt_sqn		= g_htonl (opt_sqn);
		opt_fragment->opt_frag_off	= g_htonl (offset);
		opt_fragment->opt_frag_len	= g_htonl (count);

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
		memcpy (opt_fragment + 1, buf + offset, tsdu_length);

	        header->pgm_checksum    = 0;
	        header->pgm_checksum	= pgm_checksum((char*)header, tpdu_length, 0);

/* add to transmit window */
		pgm_txw_push (transport->txw, pkt, tpdu_length);

		retval = pgm_sendto (transport,
					FALSE,
					header,
					tpdu_length,
					MSG_CONFIRM,		/* not expecting a reply */
					(struct sockaddr*)&transport->send_smr.smr_multiaddr,
					pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
		packets++;
		bytes_sent += tpdu_length + transport->iphdr_len;
//		g_trace ("INFO","%i bytes sent", tpdu_length);

		offset += tsdu_length;

	} while (offset < count);

/* release txw lock here in order to allow spms to lock mutex */
	g_static_rw_lock_writer_unlock (&transport->txw_lock);

	pgm_reset_heartbeat_spm (transport);

	transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += count;
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT] += packets;	/* assuming packets not APDUs */
	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += bytes_sent;

	return retval;
}

/* send an apdu.
 *
 * copy into receive window one or more fragments.
 */

int pgm_transport_send (
	pgm_transport_t*	transport,
	const gchar*		data,
	gsize			len,
	int			flags		/* MSG_DONTWAIT = non-blocking */
	)
{
	g_static_rw_lock_writer_lock (&transport->txw_lock);
	if ( len <= ( transport->max_tpdu - (  sizeof(struct pgm_header) +
                                               sizeof(struct pgm_data) ) ) )
	{
		return pgm_transport_send_one_copy_unlocked (transport, data, len, flags);
	}

	return pgm_transport_send_fragment_unlocked (transport, data, len);
}

/* send a vector of apdu's, lock spins per APDU to allow SPM and RDATA generation.
 */

int pgm_transport_sendv (
	pgm_transport_t*	transport,
	const struct iovec*	vector,
	int			count
	)
{
	int bytes_sent = 0;

	for (int i = 0; i < count; i++)
	{
		int retval;

		g_static_rw_lock_writer_lock (&transport->txw_lock);
		if ( vector[i].iov_len <= ( transport->max_tpdu - (  sizeof(struct pgm_header) +
    	  	                                                     sizeof(struct pgm_data) ) ) )
		{
			retval = pgm_transport_send_one_copy_unlocked (transport, vector[i].iov_base, vector[i].iov_len, 0);
		}
		else
		{
			retval = pgm_transport_send_fragment_unlocked (transport, vector[i].iov_base, vector[i].iov_len);
		}

		if (retval > 0) {
			bytes_sent += retval;
		} else {
			break;
		}
	}

	return bytes_sent >= 0 ? bytes_sent : -1;
}

static int
send_rdata (
	pgm_transport_t*	transport,
	int			sequence_number,
	gpointer		data,
	int			len
	)
{
	int retval = 0;

/* update previous odata/rdata contents */
	struct pgm_header *header = (struct pgm_header*)data;
	struct pgm_data *rdata = (struct pgm_data*)(header + 1);
	header->pgm_type        = PGM_RDATA;

/* RDATA */
        rdata->data_trail       = g_htonl (pgm_txw_trail(transport->txw));

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_checksum((char*)header, len, 0);

	retval = pgm_sendto (transport,
				TRUE,			/* with router alert */
				header,
				len,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));

/* re-set spm timer: we are already in the timer thread, no need to prod timers
 */
	g_static_mutex_lock (&transport->mutex);
	transport->spm_heartbeat_state = 1;
	transport->next_heartbeat_spm = pgm_time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];
	g_static_mutex_unlock (&transport->mutex);

	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED] += header->pgm_tsdu_length;
	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED]++;	/* impossible to determine APDU count */
	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += len + transport->iphdr_len;

	return retval;
}

/* enable FEC for this transport
 *
 * 2t can be greater than k but usually only for very bad transmission lines where parity
 * packets are also lost.
 */

int
pgm_transport_set_fec (
	pgm_transport_t*	transport,
	gboolean		enable_proactive_parity,
	gboolean		enable_ondemand_parity,
	guint			default_tgsize,		/* k, tg = transmission group */
	guint			default_h		/* 2t : parity packet length per tg */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

/* check validity of parameters */
	if ( default_tgsize > 223
		&& ( (default_h * 223.0) / default_tgsize ) < 1.0 )
	{
		g_error ("k/2t ratio too low to generate parity data.");
		return -EINVAL;
	}

	g_static_mutex_lock (&transport->mutex);
	transport->proactive_parity	= enable_proactive_parity;
	transport->ondemand_parity	= enable_ondemand_parity;

/* derive FEC values from requested parameters:
 *
 * n = 255
 * m = 8 
 *
 * for CCSDS we work from k = 223 by using shortened RS codes
 */

	{
		float tgsize = default_tgsize;
		float h = default_h;

		if (default_tgsize > 223)
		{
			float ratio2 = tgsize / h;
			tgsize /= (int)ratio2;
			h /= (int)ratio2;
		}

		float fec_n = 255.0;
		float fec_h = (int)( fec_n / (tgsize + h) ) * h;
		float fec_k = (int)(fec_n - fec_h);

		int each_packet = (fec_n - fec_k) / h;
		int total_packet = each_packet * (tgsize + h);

		int fec_padding = fec_n - total_packet;

		if (fec_padding > 0.0)
		{
			transport->fec_n = 255 - fec_padding;
			transport->fec_k = fec_n - (int)fec_h;
			transport->fec_padding = fec_padding;
		}
		else
		{
			transport->fec_n = fec_n;
			transport->fec_k = fec_k;
			transport->fec_padding = 0;
		}

		g_message ("fec: n %i k %i padding %i", transport->fec_n, transport->fec_k, transport->fec_padding);
	}

	exit (0);

	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

static GSource*
pgm_create_timer (
	pgm_transport_t*	transport
	)
{
	g_return_val_if_fail (transport != NULL, NULL);

	GSource *source = g_source_new (&g_pgm_timer_funcs, sizeof(pgm_timer_t));
	pgm_timer_t *timer = (pgm_timer_t*)source;

	timer->transport = transport;

	return source;
}

static int
pgm_add_timer_full (
	pgm_transport_t*	transport,
	gint			priority
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	GSource* source = pgm_create_timer (transport);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);

	guint id = g_source_attach (source, transport->timer_context);
	g_source_unref (source);

	return id;
}

static int
pgm_add_timer (
	pgm_transport_t*	transport
	)
{
	return pgm_add_timer_full (transport, G_PRIORITY_HIGH_IDLE);
}

/* determine which timer fires next: spm (ihb_tmr), nak_rb_ivl, nak_rpt_ivl, or nak_rdata_ivl
 * and check whether its already due.
 */

static gboolean
pgm_timer_prepare (
	GSource*		source,
	gint*			timeout
	)
{
	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	pgm_transport_t* transport = pgm_timer->transport;
	glong msec;

	g_static_mutex_lock (&transport->mutex);
	pgm_time_t expiration = transport->spm_heartbeat_state ? MIN(transport->next_heartbeat_spm, transport->next_ambient_spm) : transport->next_ambient_spm;
	pgm_time_t now = pgm_time_update_now();

	g_trace ("SPM","spm %" G_GINT64_FORMAT " usec", (gint64)expiration - (gint64)now);

/* save the nearest timer */
	transport->next_poll = pgm_timer->expiration = expiration = min_nak_expiry (expiration, transport);
	g_static_mutex_unlock (&transport->mutex);

/* advance time again to adjust for processing time out of the event loop, this
 * could cause further timers to expire even before checking for new wire data.
 */
	msec = pgm_to_msecs((gint64)expiration - (gint64)now);
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (G_MAXINT, (guint)msec);

	*timeout = (gint)msec;

	g_trace ("SPM","expiration in %i msec", (gint)msec);

	return (msec == 0);
}

static int
pgm_timer_signal (
	pgm_transport_t*	transport,
	pgm_time_t		expiration
	)
{
	int retval = 0;

	g_static_mutex_lock (&transport->mutex);
	if (pgm_time_after( transport->next_poll, expiration ))
	{
		transport->next_poll = expiration;
		g_trace ("INFO","new_peer: prod timer thread");
		const char one = '1';
		if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
			retval = -EINVAL;
		}
	}
	g_static_mutex_unlock (&transport->mutex);

	return retval;
}

static gboolean
pgm_timer_check (
	GSource*		source
	)
{
	g_trace ("SPM","pgm_timer_check");

	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	pgm_time_t now = pgm_time_update_now();

	gboolean retval = ( pgm_time_after_eq(now, pgm_timer->expiration) );
	if (!retval) g_thread_yield();
	return retval;
}

/* call all timers, assume that time_now has been updated by either pgm_timer_prepare
 * or pgm_timer_check and no other method calls here.
 */

static gboolean
pgm_timer_dispatch (
	GSource*		source,
	GSourceFunc		callback,
	gpointer		user_data
	)
{
	g_trace ("SPM","pgm_timer_dispatch");

	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	pgm_transport_t* transport = pgm_timer->transport;

	g_static_mutex_lock (&transport->mutex);
/* find which timers have expired and call each */
	if ( pgm_time_after_eq (pgm_time_now, transport->next_ambient_spm) )
	{
		send_spm_unlocked (transport);
		transport->spm_heartbeat_state = 0;
		transport->next_ambient_spm = pgm_time_now + transport->spm_ambient_interval;
	}
	else if ( transport->spm_heartbeat_state &&
		 pgm_time_after_eq (pgm_time_now, transport->next_heartbeat_spm) )
	{
		send_spm_unlocked (transport);
	
		if (transport->spm_heartbeat_interval[transport->spm_heartbeat_state])
		{
			transport->next_heartbeat_spm = pgm_time_now + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];
		} else
		{	/* transition heartbeat to ambient */
			transport->spm_heartbeat_state = 0;
		}

	}

	g_static_rw_lock_reader_lock (&transport->peers_lock);
	check_peer_nak_state (transport);
	g_static_rw_lock_reader_unlock (&transport->peers_lock);
	g_static_mutex_unlock (&transport->mutex);

	return TRUE;
}

/* TODO: this should be in on_io_data to be more streamlined, or a generic options parser.
 */

static int
get_opt_fragment (
	struct pgm_opt_header*	opt_header,
	struct pgm_opt_fragment**	opt_fragment
	)
{
	int retval = 0;

	g_assert (opt_header->opt_type == PGM_OPT_LENGTH);
	g_assert (opt_header->opt_length == sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length));
	struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);

/* always at least two options, first is always opt_length */
	do {
		opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

		if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_FRAGMENT)
		{
			*opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			retval = 1;
			goto out;
		}

	} while (!(opt_header->opt_type & PGM_OPT_END));

	*opt_fragment = NULL;

out:
	return retval;
}

/* ODATA packet with any of the following options:
 *
 * OPT_FRAGMENT - this TPDU part of a larger APDU.
 *
 * returns:
 *	0
 *	-EINVAL if pipe proding failed
 */

static int
on_odata (
	pgm_peer_t*		sender,
	struct pgm_header*	header,
	char*			data,
	int			len)
{
	g_trace ("INFO","on_odata");

	int retval = 0;
	pgm_transport_t* transport = sender->transport;
	struct pgm_data* odata = (struct pgm_data*)data;
	odata->data_sqn = g_ntohl (odata->data_sqn);

/* pre-allocate from glib allocator (not slice allocator) full APDU packet for first new fragment, re-use
 * through to event handler.
 */
	struct pgm_opt_fragment* opt_fragment;
	pgm_time_t nak_rb_expiry = pgm_time_update_now () + nak_rb_ivl(transport);

	if ((header->pgm_options & PGM_OPT_PRESENT) && get_opt_fragment((gpointer)(odata + 1), &opt_fragment))
	{
		guint16 opt_total_length = g_ntohs(*(guint16*)( (char*)( odata + 1 ) + sizeof(struct pgm_opt_header)));

		g_trace ("INFO","push fragment (sqn #%u trail #%u apdu_first_sqn #%u fragment_offset %u apdu_len %u)",
			odata->data_sqn, g_ntohl (odata->data_trail), g_ntohl (opt_fragment->opt_sqn), g_ntohl (opt_fragment->opt_frag_off), g_ntohl (opt_fragment->opt_frag_len));
		g_static_mutex_lock (&sender->mutex);
		retval = pgm_rxw_push_fragment_copy (sender->rxw,
					(char*)(odata + 1) + opt_total_length,
					g_ntohs (header->pgm_tsdu_length),
					odata->data_sqn,
					g_ntohl (odata->data_trail),
					g_ntohl (opt_fragment->opt_sqn),
					g_ntohl (opt_fragment->opt_frag_off),
					g_ntohl (opt_fragment->opt_frag_len),
					nak_rb_expiry);
	}
	else
	{
		g_static_mutex_lock (&sender->mutex);
		retval = pgm_rxw_push_copy (sender->rxw,
					odata + 1,
					g_ntohs (header->pgm_tsdu_length),
					odata->data_sqn,
					g_ntohl (odata->data_trail),
					nak_rb_expiry);
	}

	g_static_mutex_unlock (&sender->mutex);

	gboolean flush_naks = FALSE;

	switch (retval) {
	case PGM_RXW_CREATED_PLACEHOLDER:
		flush_naks = TRUE;
		break;

	case PGM_RXW_DUPLICATE:
		sender->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		break;

	case PGM_RXW_MALFORMED_APDU:
		sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA]++;

	case PGM_RXW_NOT_IN_TXW:
	case PGM_RXW_APDU_LOST:
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		break;

	default:
		break;
	}

	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED] += g_ntohs (header->pgm_tsdu_length);
	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED]++;

	if (flush_naks)
	{
/* flush out 1st time nak packets */
		g_static_mutex_lock (&transport->mutex);

		if (pgm_time_after (transport->next_poll, nak_rb_expiry))
		{
			transport->next_poll = nak_rb_expiry;
			g_trace ("INFO","on_odata: prod timer thread");
			const char one = '1';
			if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
				g_critical ("write to pipe failed :(");
				retval = -EINVAL;
			}
		}

		g_static_mutex_unlock (&transport->mutex);
	}

out:
	return retval;
}

/* identical to on_odata except for statistics
 */

static int
on_rdata (
	pgm_peer_t*		sender,
	struct pgm_header*	header,
	char*			data,
	int			len)
{
	g_trace ("INFO","on_rdata");

	int retval = 0;
	struct pgm_transport_t* transport = sender->transport;
	struct pgm_data* rdata = (struct pgm_data*)data;
	rdata->data_sqn = g_ntohl (rdata->data_sqn);

	struct pgm_opt_fragment* opt_fragment;
	pgm_time_t nak_rb_expiry = pgm_time_update_now () + nak_rb_ivl(transport);

	if ((header->pgm_options & PGM_OPT_PRESENT) && get_opt_fragment((gpointer)(rdata + 1), &opt_fragment))
	{
		guint16 opt_total_length = g_ntohs(*(guint16*)( (char*)( rdata + 1 ) + sizeof(struct pgm_opt_header)));

		g_trace ("INFO","push fragment (sqn #%u trail #%u apdu_first_sqn #%u fragment_offset %u apdu_len %u)",
			 rdata->data_sqn, g_ntohl (rdata->data_trail), g_ntohl (opt_fragment->opt_sqn), g_ntohl (opt_fragment->opt_frag_off), g_ntohl (opt_fragment->opt_frag_len));
		g_static_mutex_lock (&sender->mutex);
		retval = pgm_rxw_push_fragment (sender->rxw,
					(char*)(rdata + 1) + opt_total_length,
					g_ntohs (header->pgm_tsdu_length),
					rdata->data_sqn,
					g_ntohl (rdata->data_trail),
					g_ntohl (opt_fragment->opt_sqn),
					g_ntohl (opt_fragment->opt_frag_off),
					g_ntohl (opt_fragment->opt_frag_len),
					nak_rb_expiry);
	}
	else
	{
		g_static_mutex_lock (&sender->mutex);
		retval = pgm_rxw_push_copy (sender->rxw,
					rdata + 1,
					g_ntohs (header->pgm_tsdu_length),
					rdata->data_sqn,
					g_ntohl (rdata->data_trail),
					nak_rb_expiry);
	}
	g_static_mutex_unlock (&sender->mutex);

	gboolean flush_naks = FALSE;

	switch (retval) {
	case PGM_RXW_CREATED_PLACEHOLDER:
		flush_naks = TRUE;
		break;

	case PGM_RXW_DUPLICATE:
		sender->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		break;

	case PGM_RXW_MALFORMED_APDU:
		sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_RDATA]++;

	case PGM_RXW_NOT_IN_TXW:
	case PGM_RXW_APDU_LOST:
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		break;

	default:
		break;
	}

	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED] += g_ntohs (header->pgm_tsdu_length);
	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED]++;

	if (flush_naks)
	{
/* flush out 1st time nak packets */
		g_static_mutex_lock (&transport->mutex);

		if (pgm_time_after (transport->next_poll, nak_rb_expiry))
		{
			transport->next_poll = nak_rb_expiry;
			g_trace ("INFO","on_odata: prod timer thread");
			const char one = '1';
			if (1 != write (transport->timer_pipe[1], &one, sizeof(one))) {
				g_critical ("write to pipe failed :(");
				retval = -EINVAL;
			}
		}

		g_static_mutex_unlock (&transport->mutex);
	}

out:
	return retval;
}

/* eof */

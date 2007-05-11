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
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include "backtrace.h"
#include "log.h"
#include "pgm.h"
#include "txwi.h"
#include "rxwi.h"
#include "transport.h"
#include "sn.h"
#include "timer.h"

//#define TRANSPORT_DEBUG

#ifndef TRANSPORT_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif

/* external: Glib event loop GSource of pgm contiguous data */
struct pgm_watch {
	GSource		source;
	GPollFD		pollfd;
	struct pgm_transport*	transport;
};

/* internal: Glib event loop GSource of spm & rx state timers */
struct pgm_timer {
	GSource		source;
	guint64		expiration;
	struct pgm_transport*	transport;
};

/* callback for pgm timer events */
typedef int (*timer_callback)(struct pgm_transport*);


/* global locals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN		"transport"

static int ipproto_pgm = IPPROTO_PGM;

static gboolean pgm_src_prepare (GSource*, gint*);
static gboolean pgm_src_check (GSource*);
static gboolean pgm_src_dispatch (GSource*, GSourceFunc, gpointer);

static GSourceFuncs g_pgm_watch_funcs = {
	pgm_src_prepare,
	pgm_src_check,
	pgm_src_dispatch,
	NULL
};

static GSource* pgm_create_timer (struct pgm_transport*);
static int pgm_add_timer_full (struct pgm_transport*, gint);
static int pgm_add_timer (struct pgm_transport*);

static gboolean pgm_timer_prepare (GSource*, gint*);
static gboolean pgm_timer_check (GSource*);
static gboolean pgm_timer_dispatch (GSource*, GSourceFunc, gpointer);

static GSourceFuncs g_pgm_timer_funcs = {
	pgm_timer_prepare,
	pgm_timer_check,
	pgm_timer_dispatch,
	NULL
};

static int send_spm (struct pgm_transport*);
static int send_nak (struct pgm_transport*, struct pgm_peer*, guint32);
static int check_nak_rb_state (struct pgm_transport*);
static int check_nak_rpt_state (struct pgm_transport*);
static int check_nak_rdata_state (struct pgm_transport*);

static int send_rdata (struct pgm_transport*, int, gpointer, int);

static void destroy_peer (gpointer, gpointer, gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);
static gboolean on_io_error (GIOChannel*, GIOCondition, gpointer);

static int on_spm (struct pgm_peer*, struct pgm_header*, char*, int);
static int on_nak (struct pgm_peer*, struct pgm_header*, char*, int);
static int on_odata (struct pgm_peer*, struct pgm_header*, char*, int);
static int on_rdata (struct pgm_peer*, struct pgm_header*, char*, int);

static int on_pgm_data (gpointer, guint, gpointer);


gchar*
pgm_print_tsi (
	struct tsi*	tsi
	)
{
	guint8* gsi = (guint8*)tsi;
	guint16 source_port = *(guint16*)(gsi + 6);
	static char buf[sizeof("000.000.000.000.000.000.00000")];
	snprintf(buf, sizeof(buf), "%i.%i.%i.%i.%i.%i.%i",
		gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (source_port));
	return buf;
}

/* convert a transport session identifier TSI to a hash value
 *  */

static guint
tsi_hash (
	gconstpointer v
        )
{
	return g_str_hash(pgm_print_tsi((struct tsi*)v));
}

/* compare two transport session identifier TSI values and return TRUE if they are equal
 *  */

static gint
tsi_equal (
	gconstpointer   v,
	gconstpointer   v2
        )
{
	return memcmp (v, v2, (6 * sizeof(guint8)) + sizeof(guint16)) == 0;
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
	if (!time_supported ()) time_init();


/* find PGM protocol id */

// TODO: fix valgrind errors
#if HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	e = getprotobyname_r("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace("Setting PGM protocol number to %i from /etc/protocols.");
			ipproto_pgm = proto->p_proto;
		}
	}
#else
	struct protoent *proto = getprotobyname("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace("Setting PGM protocol number to %i from /etc/protocols.", proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#endif

	return retval;
}

/* destroy a pgm_transport object and contents, if last transport also destroy
 * associated event loop
 */

int
pgm_transport_destroy (
	struct pgm_transport*	transport,
	gboolean		flush
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	g_mutex_lock (transport->mutex);

/* flush data by sending heartbeat SPMs & processing NAKs until ambient */
	if (flush) {
	}

/* close down receive side first to stop new data incoming */
	if (transport->recv_channel) {
		g_trace ("closing receive channel.");

		GError *err = NULL;
		g_io_channel_shutdown (transport->recv_channel, flush, &err);

/* TODO: flush GLib main loop with context specific to the recv channel */

		transport->recv_channel = NULL;
	}
	if (transport->peers) {
		g_trace ("destroying peer data.");

		g_hash_table_foreach (transport->peers, destroy_peer, transport);
		g_hash_table_destroy (transport->peers);
		transport->peers = NULL;
	}

	if (transport->txw) {
		g_trace ("destroying transmit window.");
		txw_shutdown (transport->txw);
		transport->txw = NULL;
	}

	if (transport->recv_sock) {
		puts ("closing receive socket.");
		close(transport->recv_sock);
		transport->recv_sock = 0;
	}

	if (transport->send_sock) {
		puts ("closing send socket.");
		close(transport->send_sock);
		transport->send_sock = 0;
	}
	if (transport->send_with_router_alert_sock) {
		puts ("closing send with router alert socket.");
		close(transport->send_with_router_alert_sock);
		transport->send_with_router_alert_sock = 0;
	}

	if (transport->spm_heartbeat_interval) {
		g_free (transport->spm_heartbeat_interval);
		transport->spm_heartbeat_interval = NULL;
	}

	if (transport->commit_queue) {
		g_async_queue_unref (transport->commit_queue);
		transport->commit_queue = NULL;
	}
	if (transport->commit_pipe[0]) {
		close (transport->commit_pipe[0]);
		transport->commit_pipe[0] = 0;
	}
	if (transport->commit_pipe[1]) {
		close (transport->commit_pipe[1]);
		transport->commit_pipe[1] = 0;
	}
	if (transport->trash_event) {
		gpointer *p = NULL;
		while ( (p = g_trash_stack_pop (&transport->trash_event)) )
		{
			g_slice_free1 (sizeof(struct pgm_event), p);
		}

		g_assert (transport->trash_event == NULL);
	}

	g_mutex_unlock (transport->mutex);
	g_mutex_free (transport->mutex);
	g_free (transport);

	g_trace ("finished.");
	return 0;
}

/* each transport object has a list of peers, one for each host sending PGM data.
 * this function is called to destroy each peer when the peer list is destroyed
 */

static void
destroy_peer (
	gpointer	tsi,
	gpointer	peer,
	gpointer	transport
	)
{
	rxw_shutdown (((struct pgm_peer*)peer)->rxw);
	((struct pgm_peer*)peer)->rxw = NULL;

	g_free (peer);
	peer = NULL;
}

static inline gpointer
pgm_alloc_event (
	struct pgm_transport*	transport
	)
{
	g_return_val_if_fail (transport != NULL, NULL);

	return transport->trash_event ?  g_trash_stack_pop (&transport->trash_event) : g_slice_alloc (sizeof(struct pgm_event));
}


/* create a pgm_transport object.  create sockets that require superuser priviledges, if this is
 * the first instance also create a real-time priority receiving thread.
 *
 * if send == recv only two sockets need to be created.
 *
 * all receiver addresses must be the same family.
 * interface and multiaddr must be the same family.
 */

#if ( AF_INET != PF_INET ) || ( AF_INET6 != PF_INET6 )
#error AF_INET and PF_INET are different values, the bananas are jumping in their pyjamas!
#endif

int
pgm_transport_create (
	struct pgm_transport**	transport_,
	guint8*			gsi,
	struct sock_mreq*	recv_smr,	/* receive port, multicast group & interface address */
	int			recv_len,
	struct sock_mreq*	send_smr	/* send ... */
	)
{
	g_return_val_if_fail (transport_ != NULL, -EINVAL);
	g_return_val_if_fail (recv_smr != NULL, -EINVAL);
	g_return_val_if_fail (recv_len > 0, -EINVAL);
	g_return_val_if_fail (recv_len <= IP_MAX_MEMBERSHIPS, -EINVAL);
	g_return_val_if_fail (send_smr != NULL, -EINVAL);
	for (int i = 0; i < recv_len; i++)
	{
		g_return_val_if_fail (sockaddr_family(&recv_smr[i].smr_multiaddr) == sockaddr_family(&recv_smr[0].smr_multiaddr), -EINVAL);
		g_return_val_if_fail (sockaddr_family(&recv_smr[i].smr_multiaddr) == sockaddr_family(&recv_smr[i].smr_interface), -EINVAL);

/* TODO: allow multiple ports */
		g_return_val_if_fail (sockaddr_port(&recv_smr[i].smr_multiaddr) == sockaddr_port(&recv_smr[0].smr_multiaddr), -EINVAL);
	}
	g_return_val_if_fail (sockaddr_family(&send_smr->smr_multiaddr) == sockaddr_family(&send_smr->smr_interface), -EINVAL);
	g_return_val_if_fail (sockaddr_port(&send_smr->smr_multiaddr) == sockaddr_port(&recv_smr[0].smr_multiaddr), -EINVAL);

	int retval = 0;
	struct pgm_transport* transport;

/* create transport object */
	transport = g_malloc0 (sizeof(struct pgm_transport));
	transport->mutex = g_mutex_new ();

	memcpy (transport->tsi.gsi, gsi, 6);
	transport->tsi.sport = g_htons (g_random_int_range (0, UINT16_MAX));

/* copy network parameters */
	memcpy (&transport->send_smr, send_smr, sizeof(struct sock_mreq));
	for (int i = 0; i < recv_len; i++)
	{
		memcpy (&transport->recv_smr[i], &recv_smr[i], sizeof(struct sock_mreq));
	}

/* open raw sockets to implement PGM at application layer */
	g_trace ("opening raw sockets.");
	if ((transport->recv_sock = socket(sockaddr_family(&recv_smr[0].smr_interface),
						SOCK_RAW,
						ipproto_pgm)) < 0)
	{
		retval = errno;
		if (retval == EPERM && 0 != getuid()) {
			g_critical ("PGM protocol requires this program to run as superuser.");
		}
		goto err_destroy;
	}

	if ((transport->send_sock = socket(sockaddr_family(&send_smr->smr_interface),
						SOCK_RAW,
						ipproto_pgm)) < 0)
	{
		retval = errno;
		goto err_destroy;
	}

	if ((transport->send_with_router_alert_sock = socket(sockaddr_family(&send_smr->smr_interface),
						SOCK_RAW,
						ipproto_pgm)) < 0)
	{
		retval = errno;
		goto err_destroy;
	}

/* create receiving thread */
#if 0
	GError* err;
	GThread* thread;
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

	g_static_mutex_lock (&mutex);
	thread = g_thread_create_full (pgm_thread,	/* function to call in new thread */
					NULL,
					0,		/* stack size */
					TRUE,		/* joinable */
					TRUE,		/* native thread */
					G_THREAD_PRIORITY_URGENT,	/* highest priority */
					&err);
	g_static_mutex_unlock (&mutex);
#endif

	*transport_ = transport;

	return retval;

err_destroy:
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

	g_mutex_free (transport->mutex);
	g_free (transport);
	transport = NULL;

	return retval;
}

/* drop out of setuid 0 */
void
drop_superuser (void)
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
	struct pgm_transport*	transport,
	guint16			max_tpdu
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_tpdu >= (sizeof(struct iphdr) + sizeof(struct pgm_header)), -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->max_tpdu = max_tpdu;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < hops < 256, hops == -1 use kernel default (ignored).
 */

int
pgm_transport_set_hops (
	struct pgm_transport*	transport,
	gint			hops
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (hops > 0, -EINVAL);
	g_return_val_if_fail (hops < 256, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->hops = hops;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* Linux 2.6 limited to millisecond resolution with conventional timers, however RDTSC
 * and future high-resolution timers allow nanosecond resolution.  Current ethernet technology
 * is limited to microseconds at best so we'll sit there for a bit.
 */

int
pgm_transport_set_ambient_spm (
	struct pgm_transport*	transport,
	guint			spm_ambient_interval	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (spm_ambient_interval > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->spm_ambient_interval = spm_ambient_interval;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* an array of intervals appropriately tuned till ambient period is reached.
 *
 * array is zero leaded for ambient state, and zero terminated for easy detection.
 */

int
pgm_transport_set_heartbeat_spm (
	struct pgm_transport*	transport,
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

	g_mutex_lock (transport->mutex);
	if (transport->spm_heartbeat_interval)
		g_free (transport->spm_heartbeat_interval);
	transport->spm_heartbeat_interval = g_malloc (sizeof(guint) * (len+2));
	memcpy (&transport->spm_heartbeat_interval[1], spm_heartbeat_interval, sizeof(guint) * len);
	transport->spm_heartbeat_interval[0] = transport->spm_heartbeat_interval[len] = 0;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < txw_preallocate <= txw_sqns 
 *
 * can only be enforced at bind.
 */

int
pgm_transport_set_txw_preallocate (
	struct pgm_transport*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->txw_preallocate = sqns;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < txw_sqns < one less than half sequence space
 */

int
pgm_transport_set_txw_sqns (
	struct pgm_transport*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->txw_sqns = sqns;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < secs < ( txw_sqns / txw_max_rte )
 *
 * can only be enforced upon bind.
 */

int
pgm_transport_set_txw_secs (
	struct pgm_transport*	transport,
	guint			secs
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (secs > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->txw_secs = secs;
	g_mutex_unlock (transport->mutex);

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
	struct pgm_transport*	transport,
	guint			max_rte
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_rte > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->txw_max_rte = max_rte;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < rxw_preallocate <= rxw_sqns 
 *
 * can only be enforced at bind.
 */

int
pgm_transport_set_rxw_preallocate (
	struct pgm_transport*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->rxw_preallocate = sqns;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < rxw_sqns < one less than half sequence space
 */

int
pgm_transport_set_rxw_sqns (
	struct pgm_transport*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->rxw_sqns = sqns;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < secs < ( rxw_sqns / rxw_max_rte )
 *
 * can only be enforced upon bind.
 */

int
pgm_transport_set_rxw_secs (
	struct pgm_transport*	transport,
	guint			secs
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (secs > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->rxw_secs = secs;
	g_mutex_unlock (transport->mutex);

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
	struct pgm_transport*	transport,
	guint			max_rte
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);
	g_return_val_if_fail (max_rte > 0, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->rxw_max_rte = max_rte;
	g_mutex_unlock (transport->mutex);

	return 0;
}


/* 0 < wmem < wmem_max (user)
 *
 * operating system and sysctl dependent maximum, minimum on Linux 256 (doubled).
 */

int
pgm_transport_set_sndbuf (
	struct pgm_transport*	transport,
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

	g_mutex_lock (transport->mutex);
	transport->sndbuf = size;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* 0 < rmem < rmem_max (user)
 */

int
pgm_transport_set_rcvbuf (
	struct pgm_transport*	transport,
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

	g_mutex_lock (transport->mutex);
	transport->rcvbuf = size;
	g_mutex_unlock (transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_rb_ivl (
	struct pgm_transport*	transport,
	guint			usec
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->nak_rb_ivl = usec;
	g_mutex_unlock (transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_rpt_ivl (
	struct pgm_transport*	transport,
	guint			usec
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->nak_rpt_ivl = usec;
	g_mutex_unlock (transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_rdata_ivl (
	struct pgm_transport*	transport,
	guint			usec
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->nak_rdata_ivl = usec;
	g_mutex_unlock (transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_data_retries (
	struct pgm_transport*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->nak_data_retries = cnt;
	g_mutex_unlock (transport->mutex);

	return 0;
}

int
pgm_transport_set_nak_ncf_retries (
	struct pgm_transport*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	g_mutex_lock (transport->mutex);
	transport->nak_ncf_retries = cnt;
	g_mutex_unlock (transport->mutex);

	return 0;
}

/* bind the sockets to the link layer to start receiving data.
 */

int
pgm_transport_bind (
	struct pgm_transport*	transport
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	int retval = 0;

	g_mutex_lock (transport->mutex);

	g_trace ("create asynchronous commit queue.");
	transport->commit_queue = g_async_queue_new();

	g_trace ("create commit pipe.");
	retval = pipe (transport->commit_pipe);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* set write end non-blocking */
	int fd_flags = fcntl (transport->commit_pipe[1], F_GETFL);
	if (fd_flags < 0) {
		retval = errno;
		goto out;
	}
	retval = fcntl (transport->commit_pipe[1], F_SETFL, fd_flags | O_NONBLOCK);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
/* set read end non-blocking */
	fcntl (transport->commit_pipe[0], F_GETFL);
	if (fd_flags < 0) {
		retval = errno;
		goto out;
	}
	retval = fcntl (transport->commit_pipe[0], F_SETFL, fd_flags | O_NONBLOCK);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
		

	g_trace ("construct transmit window.");
	transport->txw = txw_init (transport->max_tpdu,
					transport->txw_preallocate,
					transport->txw_sqns,
					transport->txw_secs,
					transport->txw_max_rte);

/* create peer list */
	transport->peers = g_hash_table_new (tsi_hash, tsi_equal);

/* include IP header only for incoming data */
	retval = sockaddr_hdrincl (transport->recv_sock, sockaddr_family(&transport->recv_smr[0].smr_interface), TRUE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* buffers, set size first then re-read to confirm actual value */
#if 0
	retval = setsockopt(transport->recv_sock, SOL_SOCKET, SO_RCVBUF, (char*)&transport->rcvbuf, sizeof(transport->rcvbuf));
	if (retval < 0) {
		retval = errno;
		goto out;
	}
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
#endif

	int buffer_size;
	socklen_t len = sizeof(buffer_size);
	retval = getsockopt(transport->recv_sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, &len);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	g_trace ("receive buffer set at %i bytes.", buffer_size);

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
	g_trace ("send buffer set at %i bytes.", buffer_size);

/* bind udp unicast sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms so callee should specify any interface.
 */
/* TODO: different ports requires a new bound socket */
	retval = bind (transport->recv_sock,
			(struct sockaddr*)&transport->recv_smr[0].smr_interface,
			sockaddr_len(&transport->recv_smr[0].smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->recv_smr[0].smr_interface, s, sizeof(s));
		g_trace ("bind failed on recv_smr[0] %s:%i", s, g_ntohs(sockaddr_port(&transport->recv_smr[0].smr_interface)));
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->recv_smr[0].smr_interface, s, sizeof(s));
		g_trace ("bind succeeded on recv_smr[0] %s:%i", s, g_ntohs(sockaddr_port(&transport->recv_smr[0].smr_interface)));
	}
#endif

	retval = bind (transport->send_sock,
			(struct sockaddr*)&transport->send_smr.smr_interface,
			sockaddr_len(&transport->send_smr.smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("bind failed on send_smr %s:%i", s, g_ntohs(sockaddr_port(&transport->send_smr.smr_interface)));
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("bind succeeded on send_smr %s:%i", s, g_ntohs(sockaddr_port(&transport->send_smr.smr_interface)));
	}
#endif

	retval = bind (transport->send_with_router_alert_sock,
			(struct sockaddr*)&transport->send_smr.smr_interface,
			sockaddr_len(&transport->send_smr.smr_interface));
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("bind (router alert) failed on send_smr %s:%i", s, sockaddr_port(&transport->send_smr.smr_interface));
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_interface, s, sizeof(s));
		g_trace ("bind (router alert) succeeded on send_smr %s:%i", s, sockaddr_port(&transport->send_smr.smr_interface));
	}
#endif

/* receiving groups (multiple) */
/* TODO: add IPv6 multicast membership? */
	struct sock_mreq* p = transport->recv_smr;
	int i = 1;
	do {
		retval = sockaddr_add_membership (transport->recv_sock, p);
		if (retval < 0) {
			retval = errno;
#ifdef TRANSPORT_DEBUG
			char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
			sockaddr_ntop (&p->smr_multiaddr, s1, sizeof(s1));
			sockaddr_ntop (&p->smr_interface, s2, sizeof(s2));
			g_trace ("sockaddr_add_membership failed on recv_smr[%i] %s %s", i-1, s1, s2);
#endif
			goto out;
		}
#ifdef TRANSPORT_DEBUG
		else
		{
			char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
			sockaddr_ntop (&p->smr_multiaddr, s1, sizeof(s1));
			sockaddr_ntop (&p->smr_interface, s2, sizeof(s2));
			g_trace ("sockaddr_add_membership succeeded on recv_smr[%i] %s %s", i-1, s1, s2);
		}
#endif

	} while ((i++) < IP_MAX_MEMBERSHIPS && sockaddr_family(&(++p)->smr_multiaddr) != 0);

/* send group (singular) */
	retval = sockaddr_multicast_if (transport->send_sock, &transport->send_smr);
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("sockaddr_multicast_if failed on send_smr %s %s", s1, s2);
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("sockaddr_multicast_if succeeded on send_smr %s %s", s1, s2);
	}
#endif
	retval = sockaddr_multicast_if (transport->send_with_router_alert_sock, &transport->send_smr);
	if (retval < 0) {
		retval = errno;
#ifdef TRANSPORT_DEBUG
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("sockaddr_multicast_if (router alert) failed on send_smr %s %s", s1, s2);
#endif
		goto out;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
		sockaddr_ntop (&transport->send_smr.smr_multiaddr, s1, sizeof(s1));
		sockaddr_ntop (&transport->send_smr.smr_interface, s2, sizeof(s2));
		g_trace ("sockaddr_multicast_if (router alert) succeeded on send_smr %s %s", s1, s2);
	}
#endif

/* multicast loopback */
	retval = sockaddr_multicast_loop (transport->recv_sock, sockaddr_family(&transport->recv_smr[0].smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = sockaddr_multicast_loop (transport->send_sock, sockaddr_family(&transport->send_smr.smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = sockaddr_multicast_loop (transport->send_with_router_alert_sock, sockaddr_family(&transport->send_smr.smr_interface), FALSE);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
	retval = sockaddr_multicast_hops (transport->recv_sock, sockaddr_family(&transport->recv_smr[0].smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = sockaddr_multicast_hops (transport->send_sock, sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = sockaddr_multicast_hops (transport->send_with_router_alert_sock, sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* set low packet latency preference for network elements */
	int tos = IPTOS_LOWDELAY;
	retval = sockaddr_tos (transport->send_sock, sockaddr_family(&transport->send_smr.smr_interface), tos);
	if (retval < 0) {
		retval = errno;
		goto out;
	}
	retval = sockaddr_tos (transport->send_with_router_alert_sock, sockaddr_family(&transport->send_smr.smr_interface), tos);
	if (retval < 0) {
		retval = errno;
		goto out;
	}

/* add receive socket(s) to event manager */
	transport->recv_channel = g_io_channel_unix_new (transport->recv_sock);

	/* guint event = */ g_io_add_watch (transport->recv_channel, G_IO_IN | G_IO_PRI, on_io_data, transport);
	/* guint event = */ g_io_add_watch (transport->recv_channel, G_IO_ERR | G_IO_HUP | G_IO_NVAL, on_io_error, transport);

/* create recyclable SPM packet */
	switch (sockaddr_family(&transport->recv_smr[0].smr_interface)) {
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
	memcpy (&header->pgm_gsi, transport->tsi.gsi, 6);
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= sockaddr_port(&transport->send_smr.smr_multiaddr);
	header->pgm_type	= PGM_SPM;

	sockaddr_to_nla ((struct sockaddr*)&transport->send_smr.smr_interface, (char*)&spm->spm_nla_afi);

	g_trace ("adding dynamic timer");
	transport->next_spm_expiry = time_now + transport->spm_ambient_interval;
	pgm_add_timer (transport);

/* cleanup */
	transport->bound = TRUE;
	g_mutex_unlock (transport->mutex);

	g_trace ("transport successfully created.");
	return retval;

out:
	return retval;
}

/* data incoming on receive sockets, can be from a sender or receiver, or simply bogus.
 * for IPv4 we receive the IP header to handle fragmentation, for IPv6 we cannot so no idea :(
 *
 * return TRUE to keep the event, FALSE to destroy the event.
 */

static gboolean
on_io_data (
	GIOChannel*	source,
	GIOCondition	condition,
	gpointer	data
	)
{
	g_trace ("on_io_data");

	struct pgm_transport* transport = data;

/* read the data */
	int fd = g_io_channel_unix_get_fd (source);
	char buffer[1500];
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int len = recvfrom (fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&addr, &addr_len);

#ifdef TRANSPORT_DEBUG
	char s[INET6_ADDRSTRLEN];
	sockaddr_ntop ((struct sockaddr*)&addr, s, sizeof(s));
	g_trace ("%i bytes received from %s", len, s);
#endif

/* verify IP and PGM header */
	struct pgm_header *pgm_header;
	char *packet;
	int packet_len;
	int e;
	if ((e = pgm_parse_packet(buffer, len, &pgm_header, &packet, &packet_len)) < 0)
	{
		goto out;
	}

/* calculate senders TSI */
	struct tsi tsi;
	memcpy (tsi.gsi, pgm_header->pgm_gsi, 6 * sizeof(guint8));
	tsi.sport = pgm_header->pgm_sport;
	g_trace ("tsi %s", pgm_print_tsi (&tsi));

/* search for TSI peer context or create a new one */
	struct pgm_peer* sender = g_hash_table_lookup (transport->peers, &tsi);
	if (sender == NULL)
	{
		g_trace ("new peer");

		sender = g_malloc0 (sizeof(struct pgm_peer));
		sender->mutex = g_mutex_new();
		sender->transport = transport;
		memcpy (&sender->tsi, &tsi, sizeof(tsi));
		sender->rxw = rxw_init (transport->max_tpdu,
					transport->rxw_preallocate,
					transport->rxw_sqns,
					transport->rxw_secs,
					transport->rxw_max_rte,
					on_pgm_data,
					sender);

		g_hash_table_insert (transport->peers, &sender->tsi, sender);
	}

/* handle PGM packet type */
	char *pgm_data = (char*)(pgm_header + 1);
	int pgm_len = packet_len - sizeof(pgm_header);
	switch (pgm_header->pgm_type) {
	case PGM_SPM:	on_spm (sender, pgm_header, pgm_data, pgm_len); break;
	case PGM_NAK:	on_nak (sender, pgm_header, pgm_data, pgm_len); break;

	case PGM_ODATA:	on_odata (sender, pgm_header, pgm_data, pgm_len); break;
	case PGM_RDATA: on_rdata (sender, pgm_header, pgm_data, pgm_len); break;

	case PGM_NNAK:
	case PGM_NCF:
	case PGM_SPMR:
	case PGM_POLL:
	case PGM_POLR:
	default:
		break;
	}

	return TRUE;

out:
	g_trace ("packet ignored.");
	return TRUE;
}

static gboolean
on_io_error (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	g_trace ("on_io_error()");

	GError *err;
	g_io_channel_shutdown (source, FALSE, &err);

/* TODO: no doubt do something clever here */

/* remove event */
	return FALSE;
}

/* SPM indicate start of a session, continued presence of a session, or flushing final packets
 * of a session.
 *
 * returns -EINVAL on invalid packet or duplicate SPM sequence number.
 */

static int
on_spm (
	struct pgm_peer*	sender,
	struct pgm_header*	header,
	char*			data,		/* data will be changed to host order on demand */
	int			len
	)
{
	int retval;

	if ((retval = pgm_verify_spm (header, data, len)) == 0)
	{
		struct pgm_spm* spm = (struct pgm_spm*)data;

		spm->spm_sqn = g_ntohl (spm->spm_sqn);

/* check for advancing sequence number */
		if ( guint32_gte (spm->spm_sqn, sender->spm_sqn) )
		{
			spm->spm_nla_afi = g_ntohl (spm->spm_nla_afi);

/* copy NLA for replies */
			nla_to_sockaddr ((const char*)&spm->spm_nla_afi, (struct sockaddr*)&sender->nla);

/* save sequence number */
			sender->spm_sqn = spm->spm_sqn;

/* update receive window */
			rxw_window_update (sender->rxw,
						g_ntohl (spm->spm_trail),
						g_ntohl (spm->spm_lead));
		}
		else
		{	/* does not advance SPM sequence number */
			retval = -EINVAL;
		}
	}

	return retval;
}

/* NAK requesting RDATA transmission for a sending transport, only valid if
 * sequence number(s) still in transmission window.
 *
 * we can potentially have different IP versions for the NAK packet to the send group.
 */

static int
on_nak (
	struct pgm_peer*	sender,
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval;

	if ((retval = pgm_verify_nak (header, data, len)) == 0)
	{
/* NAK_DPORT contains our transport OD_SPORT */
		if (header->pgm_dport != sender->transport->tsi.sport) {
			retval = -EINVAL;
			goto out;
		}

/* NAK_SPORT contains our transport OD_DPORT */
		if (header->pgm_sport != sockaddr_port(&sender->transport->send_smr.smr_multiaddr)) {
			retval = -EINVAL;
			goto out;
		}

		struct pgm_nak* nak = (struct pgm_nak*)data;
		
/* NAK_SRC_NLA contains our transport unicast NLA */
		struct sockaddr_storage nak_src_nla;
		nla_to_sockaddr ((const char*)&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);

		if (sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&sender->transport->send_smr.smr_interface) != 0) {
			retval = -EINVAL;
			goto out;
		}

/* NAK_GRP_NLA containers our transport multicast group */ 
		struct sockaddr_storage nak_grp_nla;
		switch (sockaddr_family(&nak_src_nla)) {
		case AF_INET:
			nla_to_sockaddr ((const char*)&nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
			break;

		case AF_INET6:
			nla_to_sockaddr ((const char*)&((struct pgm_nak6*)nak)->nak6_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
			break;
		}

		if (sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&sender->transport->send_smr.smr_multiaddr) != 0) {
			retval = -EINVAL;
			goto out;
		}

		nak->nak_sqn = g_ntohl (nak->nak_sqn);

		gpointer rdata = NULL;
		int rlen = 0;
		if (!txw_peek (sender->transport->txw, nak->nak_sqn, &rdata, &rlen))
		{
			send_rdata (sender->transport, nak->nak_sqn, rdata, rlen);
		}
		else
		{

/* not available */

		}
	}

out:
	return retval;
}


/* ambient/heartbeat SPM's
 *
 * heartbeat: ihb_tmr decaying between ihb_min and ihb_max 2x after last packet
 */

static int
send_spm (
	struct pgm_transport*	transport
	)
{
	int retval = 0;
	g_trace ("send_spm");

/* recycles a transport global packet */
	struct pgm_header *header = (struct pgm_header*)transport->spm_packet;
	struct pgm_spm *spm = (struct pgm_spm*)(header + 1);

	spm->spm_sqn		= g_htonl (transport->spm_sqn++);
	spm->spm_trail		= g_htonl (txw_lead(transport->txw));
	spm->spm_lead		= g_htonl (txw_trail(transport->txw));

/* checksum optional for SPMs */
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_cksum((char*)header, transport->spm_len, 0);

	retval = sendto (transport->send_sock,
				header,
				transport->spm_len,
				MSG_CONFIRM,			/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				sockaddr_len(&transport->send_smr.smr_multiaddr));

/* advance spm timer */
	if (!transport->spm_heartbeat_state)
	{	/* ambient */
		transport->next_spm_expiry = time_now + transport->spm_ambient_interval;
	} else if (transport->spm_heartbeat_interval[transport->spm_heartbeat_state])
	{	/* heartbeat */
		transport->next_spm_expiry = time_now + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];
	} else
	{	/* transition heartbeat to ambient */
		transport->spm_heartbeat_state = 0;
		transport->next_spm_expiry = time_now + transport->spm_ambient_interval;
	}

	return retval;
}

static int
send_nak (
	struct pgm_transport*	transport,
	struct pgm_peer*	peer,
	guint32			sequence_number
	)
{
	int retval = 0;
	gchar buf[ sizeof(struct pgm_header) + sizeof(struct pgm_nak) ];
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	memcpy (&header->pgm_gsi, transport->tsi.gsi, 6);
/* dport & sport swap over for a nak */
	header->pgm_sport	= sockaddr_port(&transport->recv_smr[0].smr_multiaddr);
	header->pgm_dport	= peer->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sequence_number);

/* source nla */
	sockaddr_to_nla ((struct sockaddr*)&peer->nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
	sockaddr_to_nla ((struct sockaddr*)&transport->recv_smr[0].smr_multiaddr, (char*)&nak->nak_grp_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_cksum((char*)header, tpdu_length, 0);

	retval = sendto (transport->send_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer->nla,
				sockaddr_len(&peer->nla));

	g_trace ("%i bytes sent", tpdu_length);

	return retval;
}

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update transport::next_nak_rb_timestamp for next expiration time.
 */

static void
nak_rb_state (
	gpointer		tsi,
	gpointer		peer_,
	gpointer		transport_
	)
{
	struct pgm_peer* peer = (struct pgm_peer*)peer_;
	struct pgm_transport* transport = (struct pgm_transport*)transport_;
	GList* list = peer->rxw->backoff_queue->tail;

	while (list)
	{
		GList* next_list_el = list->prev;
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

/* check this packet for state expiration */
		if (time_after_eq(time_now, rp->nak_rb_expiry))
		{
/* remove from this state */
			rxw_pkt_state_unlink (peer->rxw, rp);

			send_nak (transport, peer, rp->sequence_number);

			rp->state = PGM_PKT_WAIT_NCF_STATE;
			g_queue_push_head_link (peer->rxw->wait_ncf_queue, &rp->link_);
			rp->nak_rpt_expiry = rp->nak_rb_expiry + transport->nak_rpt_ivl;
			if (!transport->next_nak_rpt_expiry) {
				transport->next_nak_rpt_expiry = rp->nak_rpt_expiry;
			}
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (peer->rxw->backoff_queue->length == 0)
	{
		g_assert ((struct rxw_packet*)peer->rxw->backoff_queue->head == NULL);
		g_assert ((struct rxw_packet*)peer->rxw->backoff_queue->tail == NULL);

		transport->next_nak_rb_expiry = 0;
	}
	else
	{
		g_assert ((struct rxw_packet*)peer->rxw->backoff_queue->head);
		g_assert ((struct rxw_packet*)peer->rxw->backoff_queue->tail);

/* next next expiration time */

		if (time_after(transport->next_nak_rb_expiry, ((struct rxw_packet*)peer->rxw->backoff_queue->tail)->nak_rb_expiry))
		{
			transport->next_nak_rb_expiry = ((struct rxw_packet*)peer->rxw->backoff_queue->tail)->nak_rb_expiry;
		}
	}
}

static int
check_nak_rb_state (
	struct pgm_transport*	transport
	)
{
	int retval = 0;

/* each peer has its own receive window & lists
 * TODO: extract out a rx context to share one list in each state
 */
	g_hash_table_foreach (transport->peers, nak_rb_state, transport);

	return retval;
}

/* check WAIT_NCF_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_NCF_RETRIES
 * cancel the sequence number.
 */

static void
nak_rpt_state (
	gpointer		tsi,
	gpointer		peer_,
	gpointer		transport_
	)
{
	struct pgm_peer* peer = (struct pgm_peer*)peer_;
	struct pgm_transport* transport = (struct pgm_transport*)transport_;
	GList* list = peer->rxw->wait_ncf_queue->tail;

	guint dropped = 0;

	while (list)
	{
		GList* next_list_el = list->prev;
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

/* check this packet for state expiration */
		if (time_after_eq(time_now, rp->nak_rpt_expiry))
		{
/* remove from this state */
			rxw_pkt_state_unlink (peer->rxw, rp);

			if (++rp->ncf_retry_count > transport->nak_ncf_retries)
			{
/* cancellation */
				dropped++;
//				g_warning ("lost data #%u due to cancellation.", rp->sequence_number);
				rxw_mark_lost (peer->rxw, rp->sequence_number);
			}
			else
			{	/* retry */
				rp->state = PGM_PKT_BACK_OFF_STATE;
				g_queue_push_head_link (peer->rxw->backoff_queue, &rp->link_);
				rp->nak_rb_expiry = rp->nak_rpt_expiry + transport->nak_rb_ivl;
				if (!transport->next_nak_rb_expiry) {
					transport->next_nak_rb_expiry = rp->nak_rb_expiry;
				}
			}
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (peer->rxw->wait_ncf_queue->length == 0)
	{
		g_assert ((struct rxw_packet*)peer->rxw->wait_ncf_queue->head == NULL);
		g_assert ((struct rxw_packet*)peer->rxw->wait_ncf_queue->tail == NULL);

		transport->next_nak_rpt_expiry = 0;
	}
	else
	{
		g_assert ((struct rxw_packet*)peer->rxw->wait_ncf_queue->head);
		g_assert ((struct rxw_packet*)peer->rxw->wait_ncf_queue->tail);

/* next next expiration time */

		if (time_after(transport->next_nak_rpt_expiry, ((struct rxw_packet*)peer->rxw->wait_ncf_queue->tail)->nak_rpt_expiry))
		{
			transport->next_nak_rpt_expiry = ((struct rxw_packet*)peer->rxw->wait_ncf_queue->tail)->nak_rpt_expiry;
		}
	}

	if (dropped) {
		g_message ("dropped %u messages due to cancellation.", dropped);
	}
}

static int
check_nak_rpt_state (
	struct pgm_transport*	transport
	)
{
	int retval = 0;

	g_hash_table_foreach (transport->peers, nak_rpt_state, transport);

	return retval;
}

/* check WAIT_DATA_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_DATA_RETRIES
 * canel the sequence number.
 */

static void
nak_rdata_state (
	gpointer		tsi,
	gpointer		peer_,
	gpointer		transport_
	)
{
	struct pgm_peer* peer = (struct pgm_peer*)peer_;
	struct pgm_transport* transport = (struct pgm_transport*)transport_;
	GList* list = peer->rxw->wait_data_queue->tail;

	while (list)
	{
		GList* next_list_el = list->prev;
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

/* check this packet for state expiration */
		if (time_after_eq(time_now, rp->nak_rdata_expiry))
		{
/* remove from this state */
			rxw_pkt_state_unlink (peer->rxw, rp);

			if (++rp->data_retry_count > transport->nak_data_retries)
			{
/* cancellation */
				g_warning ("lost data #%u due to cancellation.", rp->sequence_number);
				rxw_mark_lost (peer->rxw, rp->sequence_number);
			}
			else
			{	/* retry */
				rp->state = PGM_PKT_BACK_OFF_STATE;
				g_queue_push_head_link (peer->rxw->backoff_queue, &rp->link_);
				rp->nak_rb_expiry = rp->nak_rdata_expiry + transport->nak_rb_ivl;
				if (!transport->next_nak_rb_expiry) {
					transport->next_nak_rb_expiry = rp->nak_rb_expiry;
				}
			}
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (peer->rxw->wait_data_queue->length == 0)
	{
		g_assert ((struct rxw_packet*)peer->rxw->wait_data_queue->head == NULL);
		g_assert ((struct rxw_packet*)peer->rxw->wait_data_queue->tail == NULL);

		transport->next_nak_rdata_expiry = 0;
	}
	else
	{
		g_assert ((struct rxw_packet*)peer->rxw->wait_data_queue->head);
		g_assert ((struct rxw_packet*)peer->rxw->wait_data_queue->tail);

/* next next expiration time */

		if (time_after(transport->next_nak_rdata_expiry, ((struct rxw_packet*)peer->rxw->wait_data_queue->tail)->nak_rdata_expiry))
		{
			transport->next_nak_rdata_expiry = ((struct rxw_packet*)peer->rxw->wait_data_queue->tail)->nak_rdata_expiry;
		}
	}
}

static int
check_nak_rdata_state (
	struct pgm_transport*	transport
	)
{
	int retval = 0;

	g_hash_table_foreach (transport->peers, nak_rdata_state, transport);

	return retval;
}

/* can be called from any thread, it needs to update the transmit window with the new
 * data and then send on the wire, only then can control return to the callee.
 *
 * special care is necessary with the provided memory, it must be previously allocated
 * from the transmit window, and offset to include the pgm header.
 */

int
pgm_write (
	struct pgm_transport*	transport,
	const gchar*		buf,
	gsize			count)
{
	int retval = 0;

/* retrieve packet storage from transmit window */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + count;
	char *pkt = (char*)buf - sizeof(struct pgm_header) - sizeof(struct pgm_data);

	struct pgm_header *header = (struct pgm_header*)pkt;
	struct pgm_data *odata = (struct pgm_data*)(header + 1);
	memcpy (&header->pgm_gsi, transport->tsi.gsi, 6);
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= sockaddr_port(&transport->send_smr.smr_multiaddr);
	header->pgm_type        = PGM_ODATA;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = g_htons (count);

/* ODATA */
        odata->data_sqn         = g_htonl (txw_next_lead(transport->txw));
        odata->data_trail       = g_htonl (txw_trail(transport->txw));

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_cksum((char*)header, tpdu_length, 0);

/* add to transmit window */
	txw_push (transport->txw, pkt, tpdu_length);

	retval = sendto (transport->send_sock,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				sockaddr_len(&transport->send_smr.smr_multiaddr));

	g_trace ("%i bytes sent", tpdu_length);

/* re-set spm timer */
	transport->spm_heartbeat_state = 1;
	transport->next_spm_expiry = time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state];

	return retval;
}

static int
send_rdata (
	struct pgm_transport*	transport,
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
        rdata->data_trail       = g_htonl (txw_trail(transport->txw));

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_cksum((char*)header, len, 0);

	retval = sendto (transport->send_sock,
				header,
				len,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				sockaddr_len(&transport->send_smr.smr_multiaddr));

/* re-set spm timer */
	transport->spm_heartbeat_state = 1;
	transport->next_spm_expiry = time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state];

	return retval;
}

static GSource*
pgm_create_timer (
	struct pgm_transport*	transport
	)
{
	g_return_val_if_fail (transport != NULL, NULL);

	GSource *source = g_source_new (&g_pgm_timer_funcs, sizeof(struct pgm_timer));
	struct pgm_timer *timer = (struct pgm_timer*)source;

	timer->transport = transport;

	return source;
}

static int
pgm_add_timer_full (
	struct pgm_transport*	transport,
	gint			priority
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	GSource* source = pgm_create_timer (transport);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);

	guint id = g_source_attach (source, NULL);
	g_source_unref (source);

	return id;
}

static int
pgm_add_timer (
	struct pgm_transport*	transport
	)
{
	return pgm_add_timer_full (transport, G_PRIORITY_HIGH);
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
	struct pgm_timer* pgm_timer = (struct pgm_timer*)source;
	struct pgm_transport* transport = pgm_timer->transport;
	glong msec;

	guint64 expiration = transport->next_spm_expiry;
	guint64 now = time_update_now();

	g_trace ("spm %" G_GINT64_FORMAT " usec", (gint64)expiration - (gint64)now);

	if (transport->next_nak_rb_expiry) {
		expiration = MIN(expiration, transport->next_nak_rb_expiry);
		g_trace ("nak_rb %" G_GUINT64_FORMAT " us", (gint64)transport->next_nak_rb_expiry - (gint64)now);
	}

	if (transport->next_nak_rpt_expiry) {
		expiration = MIN(expiration, transport->next_nak_rpt_expiry);
		g_trace ("nak_rpt %" G_GUINT64_FORMAT " us", (gint64)transport->next_nak_rpt_expiry - (gint64)now);
	}

	if (transport->next_nak_rdata_expiry) {
		expiration = MIN(expiration, transport->next_nak_rdata_expiry);
		g_trace ("nak_rdata %" G_GUINT64_FORMAT " us", (gint64)transport->next_nak_rdata_expiry - (gint64)now);
	}

/* advance time again to adjust for processing time out of the event loop, this
 * could cause further timers to expire even before checking for new wire data.
 */
	msec = ((gint64)expiration - (gint64)now) / 1000;
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (G_MAXINT, (guint)msec);

	*timeout = (gint)msec;
	pgm_timer->expiration = expiration;	/* save the nearest timer */

	g_trace ("expiration in %i msec", (gint)msec);

	return (msec == 0);
}

static gboolean
pgm_timer_check (
	GSource*		source
	)
{
	g_trace ("pgm_timer_check");

	struct pgm_timer* pgm_timer = (struct pgm_timer*)source;
	guint64 now = time_update_now();

	return ( time_after_eq(now, pgm_timer->expiration) );
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
	g_trace ("pgm_timer_dispatch");

	struct pgm_timer* pgm_timer = (struct pgm_timer*)source;
	struct pgm_transport* transport = pgm_timer->transport;

/* find which timers have expired and call each */
	if ( time_after_eq (time_now, transport->next_spm_expiry) )
		send_spm (transport);

	if ( transport->next_nak_rb_expiry && time_after_eq (time_now, transport->next_nak_rb_expiry) )
		check_nak_rb_state (transport);

	if ( transport->next_nak_rpt_expiry && time_after_eq (time_now, transport->next_nak_rpt_expiry) )
		check_nak_rpt_state (transport);

	if ( transport->next_nak_rdata_expiry && time_after_eq (time_now, transport->next_nak_rdata_expiry) )
		check_nak_rdata_state (transport);

	return TRUE;
}

GSource*
pgm_transport_create_watch (
	struct pgm_transport*	transport
	)
{
	g_return_val_if_fail (transport != NULL, NULL);

	GSource *source = g_source_new (&g_pgm_watch_funcs, sizeof(struct pgm_watch));
	struct pgm_watch *watch = (struct pgm_watch*)source;

	watch->transport = transport;
	watch->pollfd.fd = transport->commit_pipe[0];
	watch->pollfd.events = G_IO_IN;

	g_source_add_poll (source, &watch->pollfd);

	return source;
}

int
pgm_transport_add_watch_full (
	struct pgm_transport*	transport,
	gint			priority,
	pgm_func		func,
	gpointer		user_data,
	GDestroyNotify		notify
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (func != NULL, -EINVAL);

	GSource* source = pgm_transport_create_watch (transport);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);

	g_source_set_callback (source, (GSourceFunc)func, user_data, notify);

	guint id = g_source_attach (source, NULL);
	g_source_unref (source);

	return id;
}

int
pgm_transport_add_watch (
	struct pgm_transport*	transport,
	pgm_func		func,
	gpointer		user_data
	)
{
	return pgm_transport_add_watch_full (transport, G_PRIORITY_DEFAULT, func, user_data, NULL);
}

/* returns TRUE if source has data ready, i.e. async queue is not empty
 *
 * called before event loop poll()
 */

static gboolean
pgm_src_prepare (
	GSource*		source,
	gint*			timeout
	)
{
	struct pgm_watch* watch = (struct pgm_watch*)source;

/* infinite timeout */
	*timeout = -1;

	return ( g_async_queue_length(watch->transport->commit_queue) > 0 );
}

/* called after event loop poll()
 *
 * return TRUE if ready to dispatch.
 */

static gboolean
pgm_src_check (
	GSource*		source
	)
{
	g_trace ("pgm_src_check");

	struct pgm_watch* watch = (struct pgm_watch*)source;

	return ( g_async_queue_length(watch->transport->commit_queue) > 0 );
}

/* called when TRUE returned from prepare or check
 */

static gboolean
pgm_src_dispatch (
	GSource*		source,
	GSourceFunc		callback,
	gpointer		user_data
	)
{
	g_trace ("pgm_src_dispatch");

	pgm_func func = (pgm_func)callback;
	struct pgm_watch* watch = (struct pgm_watch*)source;

/* empty pipe */
	char buf;
	if (1 != read (watch->transport->commit_pipe[0], &buf, sizeof(buf))) {
		g_critical ("read from pipe failed :(");
	}

/* loop to purge multiple messages from asynchronous queue */
	do {
		g_async_queue_lock (watch->transport->commit_queue);

		if (g_async_queue_length_unlocked (watch->transport->commit_queue) == 0)
		{
			g_async_queue_unlock (watch->transport->commit_queue);
			break;
		}
		
		struct pgm_event* event = g_async_queue_pop_unlocked (watch->transport->commit_queue);
		g_assert (event != NULL);
		if (event->len)
			g_assert (event->len > 0 && event->data != NULL);
g_trace ("peer is %s", pgm_print_tsi(&event->peer->tsi));

		g_async_queue_unlock (watch->transport->commit_queue);

/* important that callback occurs out of lock to allow PGM layer to add more messages */
		gboolean retval = (*func) (event->data, event->len, user_data);

/* return memory to receive window */
		g_trash_stack_push (&event->peer->rxw->trash_data, event->data);
		event->data = NULL;
		g_trash_stack_push (&watch->transport->trash_event, event);

	} while (TRUE);

	return TRUE;
}


static int
on_odata (
	struct pgm_peer*	sender,
	struct pgm_header*	header,
	char*			data,
	int			len)
{
	int retval = 0;
	g_trace ("on_odata");

/* OD_SPORT matches SPM_SPORT */
	if (header->pgm_sport != sender->tsi.sport) {
g_trace("1: %u %u", header->pgm_sport, sender->transport->tsi.sport);
		retval = -EINVAL;
		goto out;
	}

/* OD_DPORT contains our transport DPORT */
	if (header->pgm_dport != sockaddr_port(&sender->transport->recv_smr[0].smr_multiaddr)) {
g_trace("2: %u %u", header->pgm_sport, sender->transport->tsi.sport);
		retval = -EINVAL;
		goto out;
	}

	struct pgm_data* odata = (struct pgm_data*)data;

	odata->data_sqn = g_ntohl (odata->data_sqn);

g_trace("psh copy");
	if (!rxw_push_copy (sender->rxw,
				odata + 1,
				g_ntohs (header->pgm_tsdu_length),
				odata->data_sqn,
				g_ntohl (odata->data_trail)))
	{
/* flush out 1st time nak packets */
		nak_rb_state (&sender->tsi, sender, sender->transport);
	}

out:
	return retval;
}

/* identical to on_odata except for statistics
 */

static int
on_rdata (
	struct pgm_peer*	sender,
	struct pgm_header*	header,
	char*			data,
	int			len)
{
	int retval = 0;
	g_trace ("on_rdata");

/* RD_SPORT matches SPM_SPORT */
	if (header->pgm_sport != sender->tsi.sport) {
		retval = -EINVAL;
		goto out;
	}

/* RD_DPORT contains our transport DPORT */
	if (header->pgm_dport != sockaddr_port(&sender->transport->recv_smr[0].smr_multiaddr)) {
		retval = -EINVAL;
		goto out;
	}

	struct pgm_data* rdata = (struct pgm_data*)data;

	rdata->data_sqn = g_ntohl (rdata->data_sqn);

	if (!rxw_push_copy (sender->rxw,
				rdata + 1,
				g_ntohs (header->pgm_tsdu_length),
				rdata->data_sqn,
				g_ntohl (rdata->data_trail)))
	{
/* flush out 1st time nak packets */
		nak_rb_state (&sender->tsi, sender, sender->transport);
	}

out:
	return retval;
}

static int
on_pgm_data (
	gpointer	data,
	guint		len,
	gpointer	peer
	)
{
	int retval = 0;
	g_trace ("on_pgm_data");

	struct pgm_transport* transport = ((struct pgm_peer*)peer)->transport;
	struct pgm_event* event = pgm_alloc_event(transport);
	event->data = data;
	event->len = len;
	event->peer = (struct pgm_peer*)peer;

g_trace ("peer is %s", pgm_print_tsi(&event->peer->tsi));
char buf[1024];
snprintf (buf, sizeof(buf), "%s", (char*)data);
g_trace ("msg: \"%s\" (%i bytes)", buf, len);

	g_async_queue_lock (transport->commit_queue);
	g_async_queue_push_unlocked (transport->commit_queue, event);

	if (g_async_queue_length_unlocked (transport->commit_queue) == 1) {
		const char one = '1';
		if (1 != write (transport->commit_pipe[1], &one, sizeof(one))) {
			g_critical ("write to pipe failed :(");
			retval = -EINVAL;
		}
	}
	g_async_queue_unlock (transport->commit_queue);

	return retval;
}

/* eof */

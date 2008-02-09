/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * HTTP administrative interface
 *
 * Copyright (c) 2006-2008 Miru Limited.
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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib.h>

#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-server-message.h>
#include <libsoup/soup-address.h>

#include <pgm/http.h>
#include <pgm/transport.h>
#include <pgm/txwi.h>
#include <pgm/rxwi.h>

#include "htdocs/404.html.h"
#include "htdocs/base.css.h"
#include "htdocs/robots.txt.h"
#include "htdocs/xhtml10_strict.doctype.h"


/* globals */

/* local globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN            "pgmhttp"

static guint16 g_server_port;

static GThread* thread;
static SoupServer* g_soup_server = NULL;
static GCond* http_cond;
static GMutex* http_mutex;

static gpointer http_thread (gpointer);
static int http_tsi_response (pgm_tsi_t*, SoupMessage*);

static void default_callback (SoupServerContext*, SoupMessage*, gpointer);
static void robots_callback (SoupServerContext*, SoupMessage*, gpointer);
static void css_callback (SoupServerContext*, SoupMessage*, gpointer);
static void index_callback (SoupServerContext*, SoupMessage*, gpointer);
static void transports_callback (SoupServerContext*, SoupMessage*, gpointer);

static void http_each_receiver (pgm_peer_t*, GString*);
static int http_receiver_response (pgm_peer_t*, SoupMessage*);


int
pgm_http_init (
	guint16		server_port
	)
{
	int retval = 0;
	GError* err;
	GThread* tmp_thread;

	g_type_init ();

/* ensure threading enabled */
	if (!g_thread_supported ()) g_thread_init (NULL);

	http_mutex = g_mutex_new();
	http_cond = g_cond_new();

	g_server_port = server_port;

	tmp_thread = g_thread_create_full (http_thread,
					NULL,
					0,		/* stack size */
					TRUE,		/* joinable */
					TRUE,		/* native thread */
					G_THREAD_PRIORITY_LOW,	/* lowest */
					&err);
	if (!tmp_thread) {
		g_error ("thread failed: %i %s", err->code, err->message);
		goto err_destroy;
	}

	thread = tmp_thread;

/* spin lock around condition waiting for thread startup */
	g_mutex_lock (http_mutex);
	while (!g_soup_server)
		g_cond_wait (http_cond, http_mutex);
	g_mutex_unlock (http_mutex);

	g_mutex_free (http_mutex);
	http_mutex = NULL;
	g_cond_free (http_cond);
	http_cond = NULL;

out:
	return retval;

err_destroy:
	return 0;
}

int
pgm_http_shutdown (void)
{
	if (g_soup_server) {
		g_object_unref (g_soup_server);
		g_soup_server = NULL;
	}

	return 0;
}

static gpointer
http_thread (
	gpointer	data
	)
{
	GMainContext* context = g_main_context_new ();

	g_message ("starting soup server.");
	g_mutex_lock (http_mutex);
	g_soup_server = soup_server_new (SOUP_SERVER_PORT, g_server_port,
					SOUP_SERVER_ASYNC_CONTEXT, context,
					NULL);
	if (!g_soup_server) {
		g_warning ("soup server failed startup: %s", strerror (errno));
		goto out;
	}

	char hostname[NI_MAXHOST + 1];
	gethostname (hostname, sizeof(hostname));

	g_message ("web interface: http://%s:%i",
			hostname,
			soup_server_get_port (g_soup_server));

	soup_server_add_handler (g_soup_server, NULL,		NULL, default_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/robots.txt",	NULL, robots_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/base.css",	NULL, css_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/",		NULL, index_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/transports",	NULL, transports_callback, NULL, NULL);

/* signal parent thread we are ready to run */
	g_cond_signal (http_cond);
	g_mutex_unlock (http_mutex);

	soup_server_run (g_soup_server);
	g_object_unref (g_soup_server);

out:
	g_main_context_unref (context);
	return NULL;
}

/* add xhtml doctype and head, populate with runtime values
 */

static GString*
http_create_response (
	const gchar*		subtitle
	)
{
	char hostname[NI_MAXHOST + 1];
	gethostname (hostname, sizeof(hostname));

/* surprising deficiency of GLib is no support of display locale time */
	char buf[100];
	time_t nowdate = time(NULL);
	struct tm now;
	localtime_r (&nowdate, &now);
	gsize ret = strftime (buf, sizeof(buf), "%c", &now);
	gsize bytes_written;
	gchar* timestamp = g_locale_to_utf8 (buf, ret, NULL, &bytes_written, NULL);

	GString* response = g_string_new (WWW_XHTML10_STRICT_DOCTYPE);
	g_string_append_printf (response, "<head>"
						"<title>%s - %s</title>"
						"<link ref=\"stylesheet\" href=\"/base.css\" type=\"text/css\" charset=\"utf-8\" />"
					"</head>"
					"<body>"
					"<div id=\"header\">"
						"<div id=\"hostname\">%s</div>"
						"<div id=\"banner\">OpenPGM - $version</div>"
						"<div id=\"timestamp\">%s</div>"
					"</div>"
					"<div id=\"navigation\">"
						"<ul>"
							"<li><a href=\"/\">General Information</a></li>"
							"<li><a href=\"/transports\">Transports</a></li>"
						"</ul>"
					"<div id=\"content\">",
				hostname,
				subtitle,
				hostname,
				timestamp );

	g_free (timestamp);

	return response;
}

static void
http_finalize_response (
	GString*		response,
	SoupMessage*		msg
	)
{
	g_string_append (response,	"</div>"
					"</body>"
					"</html>");

	gchar* buf = g_string_free (response, FALSE);
	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_SYSTEM_OWNED,
					buf, strlen(buf));
}
	
static void
robots_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	if (context->method_id != SOUP_METHOD_ID_GET) {
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
		return;
	}

	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/plain", SOUP_BUFFER_STATIC,
					WWW_ROBOTS_TXT, strlen(WWW_ROBOTS_TXT));
}

static void
css_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	if (context->method_id != SOUP_METHOD_ID_GET) {
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
		return;
	}

	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/css", SOUP_BUFFER_STATIC,
					WWW_BASE_CSS, strlen(WWW_BASE_CSS));
}

static void
index_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	GString *response;

	char hostname[NI_MAXHOST + 1];
	gethostname (hostname, sizeof(hostname));

	char username[LOGIN_NAME_MAX + 1];
	getlogin_r (username, sizeof(username));

	char ipaddress[INET6_ADDRSTRLEN];
	struct addrinfo hints, *res = NULL;
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	getaddrinfo (hostname, NULL, &hints, &res);
	inet_ntop (	res->ai_family,
			&((struct sockaddr_in*)res->ai_addr)->sin_addr,
			ipaddress,
			sizeof(ipaddress) );
	freeaddrinfo (res);

	int transport_count;
	g_static_rw_lock_reader_lock (&pgm_transport_list_lock);
	transport_count = g_slist_length (pgm_transport_list);
	g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);

	int pid = getpid();

	response = http_create_response ("OpenPGM");
	g_string_append_printf (response,	"<h1>General Information</h1>"
						"<table>"
						"<tr>"
							"<td>host name:</td><td>%s</td>"
						"</tr><tr>"
							"<td>user name:</td><td>%s</td>"
						"</tr><tr>"
							"<td>IP address:</td><td>%s</td>"
						"</tr><tr>"
							"<td>transports:</td><td><a href=\"/transports\">%i</a></td>"
						"</tr><tr>"
							"<td>process ID:</td><td>%i</td>"
						"</tr>"
						"</table>",
				hostname,
				username,
				ipaddress,
				transport_count,
				pid);

	http_finalize_response (response, msg);
}

static void
transports_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	GString *response;

	response = http_create_response ("Transports");
	g_string_append (response,	"<h1>Transports</h1>"
						"<table>"
						"<tr>"
							"<th>Group address</th>"
							"<th>Dest port</th>"
							"<th>Source GSI</th>"
							"<th>Source port</th>"
						"</tr>"
				);

	g_static_rw_lock_reader_lock (&pgm_transport_list_lock);

	GSList* list = pgm_transport_list;
	while (list)
	{
		GSList* next = list->next;
		pgm_transport_t* transport = list->data;

		char group_address[INET6_ADDRSTRLEN];
		inet_ntop (	pgm_sockaddr_family( &transport->send_smr.smr_multiaddr ),
				pgm_sockaddr_addr( &transport->send_smr.smr_multiaddr ),
				group_address,
				sizeof(group_address) );

		int dport = g_ntohs (transport->dport);

		char gsi[sizeof("000.000.000.000.000.000")];
		snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
			transport->tsi.gsi.identifier[0],
			transport->tsi.gsi.identifier[1],
			transport->tsi.gsi.identifier[2],
			transport->tsi.gsi.identifier[3],
			transport->tsi.gsi.identifier[4],
			transport->tsi.gsi.identifier[5]);

		int sport = g_ntohs (transport->tsi.sport);


		g_string_append_printf (response,	"<tr>"
								"<td>%s</td>"
								"<td>%i</td>"
								"<td><a href=\"/%s.%hu\">%s</a></td>"
								"<td><a href==\"/%s.%hu\">%hu</a></td>"
							"</tr>",
					group_address,
					dport,
					gsi, sport,
					gsi,
					gsi, sport,
					sport);

		list = next;
	}

	g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);

	g_string_append (response,		"</table>");
	http_finalize_response (response, msg);
}

static void
default_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	if (context->method_id != SOUP_METHOD_ID_GET) {
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
		return;
	}

/* magical mysterious GSI hunting from path */
	char *path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);

	pgm_tsi_t tsi;
	int count = sscanf (path, "/%hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hu",
				(unsigned char*)&tsi.gsi.identifier[0],
				(unsigned char*)&tsi.gsi.identifier[1],
				(unsigned char*)&tsi.gsi.identifier[2],
				(unsigned char*)&tsi.gsi.identifier[3],
				(unsigned char*)&tsi.gsi.identifier[4],
				(unsigned char*)&tsi.gsi.identifier[5],
				&tsi.sport);
	tsi.sport = g_htons (tsi.sport);
	g_free (path);
	if (count == 7)
	{
		int retval = http_tsi_response (&tsi, msg);
		if (!retval) return;
	}

	soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
					WWW_404_HTML, strlen(WWW_404_HTML));
}

static int
http_tsi_response (
	pgm_tsi_t*	tsi,
	SoupMessage*	msg
	)
{
/* first verify this is a valid TSI */
	g_static_rw_lock_reader_lock (&pgm_transport_list_lock);

	pgm_transport_t* transport = NULL;
	GSList* list = pgm_transport_list;
	while (list)
	{
		pgm_transport_t* list_transport = (pgm_transport_t*)list->data;
		GSList* next = list->next;

/* check source */
		if (pgm_tsi_equal (tsi, &list_transport->tsi))
		{
			transport = list_transport;
			break;
		}

/* check receivers */
		g_static_rw_lock_reader_lock (&list_transport->peers_lock);
		pgm_peer_t* receiver = g_hash_table_lookup (list_transport->peers_hashtable, tsi);
		if (receiver) {
			int retval = http_receiver_response (receiver, msg);
			g_static_rw_lock_reader_unlock (&list_transport->peers_lock);
			g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);
			return retval;
		}
		g_static_rw_lock_reader_unlock (&list_transport->peers_lock);

		list = next;
	}

	if (!transport) {
		g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);
		return -1;
	}

/* transport now contains valid matching TSI */
	char gsi[sizeof("000.000.000.000.000.000")];
	snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
			transport->tsi.gsi.identifier[0],
			transport->tsi.gsi.identifier[1],
			transport->tsi.gsi.identifier[2],
			transport->tsi.gsi.identifier[3],
			transport->tsi.gsi.identifier[4],
			transport->tsi.gsi.identifier[5]);

	char title[ sizeof("Transport 000.000.000.000.000.000.00000") ];
	sprintf (title, "Transport %s.%i",
		gsi,
		g_ntohs (transport->tsi.sport));

	char source_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &transport->send_smr.smr_interface ),
			pgm_sockaddr_addr( &transport->send_smr.smr_interface ),
			source_address,
			sizeof(source_address) );

	char group_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &transport->send_smr.smr_multiaddr ),
			pgm_sockaddr_addr( &transport->send_smr.smr_multiaddr ),
			group_address,
			sizeof(group_address) );

	int dport = g_ntohs (transport->dport);
	int sport = g_ntohs (transport->tsi.sport);

	gint ihb_min = 0;			/* need to bind first */
	gint ihb_max = 0;

	char spm_path[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &transport->recv_smr[0].smr_interface ),
			pgm_sockaddr_addr( &transport->recv_smr[0].smr_interface ),
			spm_path,
			sizeof(spm_path) );

	GString* response = http_create_response (title);
	g_string_append_printf (response,	"<h1>%s</h1>"
						"<h2>Source information</h2>"
						"<table>"
						"<tr>"
							"<td>Source address</td><td>%s</td>"
						"</tr><tr>"
							"<td>Group address</td><td>%s</td>"
						"</tr><tr>"
							"<td>Dest port</td><td>%i</td>"
						"</tr><tr>"
							"<td>Source GSI</td><td>%s</td>"
						"</tr><tr>"
							"<td>Source port</td><td>%i</td>"
						"</tr>"
						"</table>",
				title,
				source_address,
				group_address,
				dport,
				gsi,
				sport);

/* list receivers in a table */
	g_string_append (response,		"<h2>Receivers</h2>"
						"<table>"
						"<tr>"
							"<th>Group address</th>"
							"<th>Dest port</th>"
							"<th>Source address</th>"
							"<th>Last hop</th>"
							"<th>Source GSI</th>"
							"<th>Source port</th>"
						"</tr>"
			);

	g_static_rw_lock_reader_lock (&transport->peers_lock);
	GList* peers_list = transport->peers_list;
	while (peers_list) {
		GList* next = peers_list->next;
		http_each_receiver (peers_list->data, response);
		peers_list = next;
	}
	g_static_rw_lock_reader_unlock (&transport->peers_lock);

	g_string_append (response,		"</table>");

/* continue with source information */

	g_string_append_printf (response,	"<h2>Configuration information</h2>"
						"<table>"
						"<tr>"
							"<td>Ttl</td><td>%i</td>"
						"</tr><tr>"
							"<td>Adv Mode</td><td>data(1)</td>"
						"</tr><tr>"
							"<td>Late join</td><td>disable(2)</td>"
						"</tr><tr>"
							"<td>TXW_MAX_RTE</td><td>%i</td>"
						"</tr><tr>"
							"<td>TXW_SECS</td><td>%i</td>"
						"</tr><tr>"
							"<td>TXW_ADV_SECS</td><td>0</td>"
						"</tr><tr>"
							"<td>Ambient SPM interval</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>IHB_MIN</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>IHB_MAX</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>NAK_BO_IVL</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>FEC</td><td>disabled(1)</td>"
						"</tr><tr>"
							"<td>Source Path Address</td><td>%s</td>"
						"</tr>"
						"</table>",
				transport->hops,
				transport->txw_max_rte,
				transport->txw_secs,
				pgm_to_msecs(transport->spm_ambient_interval),
				ihb_min,
				ihb_max,
				pgm_to_msecs(transport->nak_bo_ivl),
				spm_path);

	g_string_append_printf (response,	"<h2>Performance information</h2>"
						"<table>"
						"<tr>"
							"<td>Data bytes sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>Data packets sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes buffered</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets buffered</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>Raw NAKs received</td><td>%i</td>"
						"</tr><tr>"
							"<td>Checksum errors</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NAKs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets discarded</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes retransmitted</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets retransmitted</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs received</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs ignored</td><td>%i</td>"
						"</tr><tr>"
							"<td>Transmission rate</td><td>%i bps</td>"
						"</tr><tr>"
							"<td>NNAK packets received</td><td>%i</td>"
						"</tr><tr>"
							"<td>NNAKs received</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NNAKs</td><td>%i</td>"
						"</tr>"
						"</table>",
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT],
						((pgm_txw_t*)transport->txw)->bytes_in_window,	/* minus IP & any UDP header */
						((pgm_txw_t*)transport->txw)->packets_in_window,
						transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS],
						transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_IGNORED],
						transport->cumulative_stats[PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]);

	g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);

	http_finalize_response (response, msg);

	return 0;
}

static void
http_each_receiver (
	pgm_peer_t*	peer,
	GString*	response
	)
{
	char group_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->group_nla ),
			pgm_sockaddr_addr( &peer->group_nla ),
			group_address,
			sizeof(group_address) );

	int dport = g_ntohs (peer->transport->dport);	/* by definition must be the same */

	char source_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->nla ),
			pgm_sockaddr_addr( &peer->nla ),
			source_address,
			sizeof(source_address) );

	char last_hop[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->local_nla ),
			pgm_sockaddr_addr( &peer->local_nla ),
			last_hop,
			sizeof(last_hop) );

	char gsi[sizeof("000.000.000.000.000.000")];
	snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
			peer->tsi.gsi.identifier[0],
			peer->tsi.gsi.identifier[1],
			peer->tsi.gsi.identifier[2],
			peer->tsi.gsi.identifier[3],
			peer->tsi.gsi.identifier[4],
			peer->tsi.gsi.identifier[5]);

	int sport = g_ntohs (peer->tsi.sport);

	g_string_append_printf (response,	"<tr>"
							"<td>%s</td>"
							"<td>%i</td>"
							"<td>%s</td>"
							"<td>%s</td>"
							"<td><a href=\"/%s.%hu\">%s</a></td>"
							"<td><a href=\"/%s.%hu\">%hu</a></td>"
						"</tr>",
				group_address,
				dport,
				source_address,
				last_hop,
				gsi, sport,
				gsi,
				gsi, sport,
				sport
			);
}

static int
http_receiver_response (
	pgm_peer_t*	peer,
	SoupMessage*	msg
	)
{
	char gsi[sizeof("000.000.000.000.000.000")];
	snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
			peer->tsi.gsi.identifier[0],
			peer->tsi.gsi.identifier[1],
			peer->tsi.gsi.identifier[2],
			peer->tsi.gsi.identifier[3],
			peer->tsi.gsi.identifier[4],
			peer->tsi.gsi.identifier[5]);

	char title[ sizeof("Receiver 000.000.000.000.000.000.00000") ];
	sprintf (title, "Receiver %s.%hu",
		gsi,
		g_ntohs (peer->tsi.sport));

	char group_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->group_nla ),
			pgm_sockaddr_addr( &peer->group_nla ),
			group_address,
			sizeof(group_address) );

	int dport = g_ntohs (peer->transport->dport);	/* by definition must be the same */

	char source_address[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->nla ),
			pgm_sockaddr_addr( &peer->nla ),
			source_address,
			sizeof(source_address) );

	char last_hop[INET6_ADDRSTRLEN];
	inet_ntop (	pgm_sockaddr_family( &peer->local_nla ),
			pgm_sockaddr_addr( &peer->local_nla ),
			last_hop,
			sizeof(last_hop) );

	int sport = g_ntohs (peer->tsi.sport);

	guint outstanding_naks = ((pgm_rxw_t*)peer->rxw)->backoff_queue->length 
				+ ((pgm_rxw_t*)peer->rxw)->wait_ncf_queue->length
				+ ((pgm_rxw_t*)peer->rxw)->wait_data_queue->length;

	char buf[100];
	time_t last_activity_time = pgm_to_secs(peer->last_packet);
	struct tm last_activity_tm;
	localtime_r (&last_activity_time, &last_activity_tm);
	gsize ret = strftime (buf, sizeof(buf), "%c", &last_activity_tm);
	gsize bytes_written;
	gchar* last_activity = g_locale_to_utf8 (buf, ret, NULL, &bytes_written, NULL);


	GString* response = http_create_response (title);
	g_string_append_printf (response,	"<h1>%s</h1>"
						"<h2>Receiver information</h2>"
						"<table>"
						"<tr>"
							"<td>Group address</td><td>%s</td>"
						"</tr><tr>"
							"<td>Dest port</td><td>%i</td>"
						"</tr><tr>"
							"<td>Source address</td><td>%s</td>"
						"</tr><tr>"
							"<td>Last hop</td><td>%s</td>"
						"</tr><tr>"
							"<td>Source GSI</td><td>%s</td>"
						"</tr><tr>"
							"<td>Source port</td><td>%i</td>"
						"</tr>"
						"</table>",
				title,
				group_address,
				dport,
				source_address,
				last_hop,
				gsi,
				sport);

	g_string_append_printf (response,	"<h2>Configuration information</h2>"
						"<table>"
						"<tr>"
							"<td>NAK_BO_IVL</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>NAK_RPT_IVL</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>NAK_NCF_RETRIES</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK_RDATA_IVL</td><td>%i ms</td>"
						"</tr><tr>"
							"<td>NAK_DATA_RETRIES</td><td>%i</td>"
						"</tr><tr>"
							"<td>Send NAKs</td><td>enabled(1)</td>"
						"</tr><tr>"
							"<td>Late join</td><td>disabled(2)</td>"
						"</tr><tr>"
							"<td>NAK TTL</td><td>%i</td>"
						"</tr><tr>"
							"<td>Delivery order</td><td>ordered(2)</td>"
						"</tr><tr>"
							"<td>Multicast NAKs</td><td>disabled(2)</td>"
						"<tr>",
						pgm_to_msecs(peer->transport->nak_bo_ivl),
						pgm_to_msecs(peer->transport->nak_rpt_ivl),
						peer->transport->nak_ncf_retries,
						pgm_to_msecs(peer->transport->nak_rdata_ivl),
						peer->transport->nak_data_retries,
						peer->transport->hops);

	g_string_append_printf (response,	"<h2>Performance information</h2>"
						"<table>"
						"<tr>"
							"<td>Data bytes received</td><td>%i</td>"
						"</tr><tr>"
							"<td>Data packets received</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK failures</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes received</td><td>%i</td>"
						"</tr><tr>"
							"<td>Checksum errors</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed SPMs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed ODATA</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed RDATA</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NCFs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets discarded</td><td>%i</td>"
						"</tr><tr>"
							"<td>Losses</td><td>%i</td>"	/* detected missed packets */
						"</tr><tr>"
							"<td>Bytes delivered to app</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets delivered to app</td><td>%i</td>"
						"</tr><tr>"
							"<td>Duplicate SPMs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Duplicate ODATA/RDATA</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK packets sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs retransmitted</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs failed</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs failed due to RXW advance</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs failed due to NCF retries</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs failed due to DATA retries</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK failures delivered to app</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs suppressed</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NAKs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Outstanding NAKs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Last activity</td><td>%s</td>"
						"</tr><tr>"
							"<td>NAK repair min time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK repair mean time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK repair max time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK fail min time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK fail mean time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK fail max time</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK min retransmit count</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK mean retransmit count</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAK max retransmit count</td><td>%i</td>"
						"</tr>"
						"</table>",
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAILURES],
						peer->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED],
						peer->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_RDATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS],
						peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED],
						peer->cumulative_stats[PGM_PC_RECEIVER_LOSSES],
						peer->cumulative_stats[PGM_PC_RECEIVER_BYTES_DELIVERED_TO_APP],
						peer->cumulative_stats[PGM_PC_RECEIVER_MSGS_DELIVERED_TO_APP],
						peer->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS],
						peer->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_RETRANSMITTED],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_FAILED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_RXW_ADVANCED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAILURES_DELIVERED],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS],
						outstanding_naks,
						last_activity,
						((pgm_rxw_t*)peer->rxw)->min_fill_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_SVC_TIME_MEAN],
						((pgm_rxw_t*)peer->rxw)->max_fill_time,
						peer->min_fail_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAIL_TIME_MEAN],
						peer->max_fail_time,
						((pgm_rxw_t*)peer->rxw)->min_nak_transmit_count,
						peer->cumulative_stats[PGM_PC_RECEIVER_TRANSMIT_MEAN],
						((pgm_rxw_t*)peer->rxw)->max_nak_transmit_count);

	http_finalize_response (response, msg);

	g_free( last_activity );

	return 0;
}


/* eof */

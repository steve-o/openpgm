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
static int http_gsi_response (pgm_gsi_t*, SoupMessage*);

static void default_callback (SoupServerContext*, SoupMessage*, gpointer);
static void robots_callback (SoupServerContext*, SoupMessage*, gpointer);
static void css_callback (SoupServerContext*, SoupMessage*, gpointer);
static void index_callback (SoupServerContext*, SoupMessage*, gpointer);
static void transports_callback (SoupServerContext*, SoupMessage*, gpointer);
static void gsi_callback (SoupServerContext*, SoupMessage*, gpointer);


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
								"<td><a href=\"/%s\">%s</a></td>"
								"<td>%i</td>"
							"</tr>",
					group_address,
					dport,
					gsi,
					gsi,
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

	pgm_gsi_t gsi;
	int count = sscanf (path, "/%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
				(unsigned char*)&gsi.identifier[0],
				(unsigned char*)&gsi.identifier[1],
				(unsigned char*)&gsi.identifier[2],
				(unsigned char*)&gsi.identifier[3],
				(unsigned char*)&gsi.identifier[4],
				(unsigned char*)&gsi.identifier[5]);
	g_free (path);
	if (count == 6)
	{
		int retval = http_gsi_response (&gsi, msg);
		if (!retval) return;
	}

	soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
					WWW_404_HTML, strlen(WWW_404_HTML));
}

static int
http_gsi_response (
	pgm_gsi_t*	query_gsi,
	SoupMessage*	msg
	)
{
/* first verify this is a valid GSI */
	g_static_rw_lock_reader_lock (&pgm_transport_list_lock);

	pgm_transport_t* transport = NULL;
	GSList* list = pgm_transport_list;
	while (list)
	{
		GSList* next = list->next;

		if (pgm_gsi_equal (query_gsi, &((pgm_transport_t*)list->data)->tsi.gsi))
		{
			transport = list->data;
			break;
		}

		list = next;
	}

	if (!transport) {
		g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);
		return -1;
	}

/* transport now contains valid matching GSI */
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
		transport->tsi.sport);

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
	char* spm_path = source_address;	/* the difference? */

	GString* response = http_create_response (title);
	g_string_append_printf (response,	"<h1>%s</h1>"
						"<h2>General information</h2>"
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

	g_string_append_printf (response,	"<h2>Sender information</h2>"
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
							"<td>NAK_RB_IVL</td><td>%i ms</td>"
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
				pgm_to_msecs(transport->nak_rb_ivl),
				spm_path);

	g_string_append_printf (response,	"<h2>Performance information</h2>"
						"<table>"
						"<tr>"
							"<td>Data bytes sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>Data msgs sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes retransmitted</td><td>%i</td>"
						"</tr><tr>"
							"<td>Msgs retransmitted</td><td>%i</td>"
						"</tr><tr>"
							"<td>Bytes sent</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs received</td><td>%i</td>"
						"</tr><tr>"
							"<td>NAKs ignored</td><td>%i</td>"
						"</tr><tr>"
							"<td>Checksum errors</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NAKs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Packets discarded</td><td>%i</td>"
						"</tr><tr>"
							"<td>SNs NAKed</td><td>%i</td>"
						"</tr><tr>"
							"<td>Transmission rate</td><td>%i bps</td>"
						"</tr><tr>"
							"<td>NNAKs received</td><td>%i</td>"
						"</tr><tr>"
							"<td>SNs NAKed in NNAKs</td><td>%i</td>"
						"</tr><tr>"
							"<td>Malformed NNAKs</td><td>%i</td>"
						"</tr>"
						"</table>",
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_BYTES_RETRANSMITTED],
						transport->cumulative_stats[PGM_PC_SOURCE_MSGS_RETRANSMITTED],
						transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_RAW_NAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_NAKS_IGNORED],
						transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS],
						transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED],
						transport->cumulative_stats[PGM_PC_SOURCE_NAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE],
						transport->cumulative_stats[PGM_PC_SOURCE_NNAK_PACKETS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_NNAKS_RECEIVED],
						transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]);

	g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);

	http_finalize_response (response, msg);

	return 0;
}

/* eof */

/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * HTTP administrative interface
 *
 * Copyright (c) 2006-2009 Miru Limited.
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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#ifdef CONFIG_LIBSOUP22
#	include <libsoup/soup-server-message.h>
#endif
#include <libsoup/soup-address.h>

#include "pgm/ip.h"
#include "pgm/http.h"
#include "pgm/transport.h"
#include "pgm/txwi.h"
#include "pgm/rxwi.h"
#include "pgm/version.h"
#include "pgm/histogram.h"
#include "pgm/getifaddrs.h"
#include "pgm/nametoindex.h"

#include "htdocs/404.html.h"
#include "htdocs/base.css.h"
#include "htdocs/robots.txt.h"
#include "htdocs/xhtml10_strict.doctype.h"

/* OpenSolaris */
#ifndef LOGIN_NAME_MAX
#	define LOGIN_NAME_MAX		256
#endif

#ifdef CONFIG_HAVE_SPRINTF_GROUPING
#	define GROUP_FORMAT			"'"
#else
#	define GROUP_FORMAT			""
#endif


/* globals */

/* local globals */

static SoupServer*	g_soup_server = NULL;
static GThread*		g_thread;
static GCond*		g_thread_cond;
static GMutex*		g_thread_mutex;
static GError*		g_error;
static char		g_hostname[NI_MAXHOST + 1];
static char		g_address[INET6_ADDRSTRLEN];
static char		g_username[LOGIN_NAME_MAX + 1];
static int		g_pid;

static gpointer http_thread (gpointer);
static int http_tsi_response (pgm_tsi_t*, SoupMessage*);

#ifdef CONFIG_LIBSOUP22
static void default_callback (SoupServerContext*, SoupMessage*, gpointer);
static void robots_callback (SoupServerContext*, SoupMessage*, gpointer);
static void css_callback (SoupServerContext*, SoupMessage*, gpointer);
static void index_callback (SoupServerContext*, SoupMessage*, gpointer);
static void interfaces_callback (SoupServerContext*, SoupMessage*, gpointer);
static void transports_callback (SoupServerContext*, SoupMessage*, gpointer);
static void histograms_callback (SoupServerContext*, SoupMessage*, gpointer);
#else
static void default_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void robots_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void css_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void index_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void interfaces_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void transports_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
static void histograms_callback (SoupServer*, SoupMessage*, const char*, GHashTable*, SoupClientContext*, gpointer);
#endif

static void http_each_receiver (pgm_peer_t*, GString*);
static int http_receiver_response (pgm_peer_t*, SoupMessage*);

static PGMHTTPError pgm_http_error_from_errno (gint);
static PGMHTTPError pgm_http_error_from_eai_errno (gint);


gboolean
pgm_http_init (
	guint16			http_port,
	GError**		error
	)
{
	g_return_val_if_fail (NULL == g_soup_server, FALSE);

	g_type_init ();

/* ensure threading enabled */
	if (!g_thread_supported ()) g_thread_init (NULL);

/* resolve (relatively) constant host details */
	if (0 != gethostname (g_hostname, sizeof(g_hostname))) {
		g_set_error (error,
			     PGM_HTTP_ERROR,
			     pgm_http_error_from_errno (errno),
			     _("Resolving hostname: %s"),
			     g_strerror (errno));
		return FALSE;
	}
	struct addrinfo hints = {
		.ai_family	= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= IPPROTO_TCP,
		.ai_flags	= AI_ADDRCONFIG
	}, *res = NULL;
	int e = getaddrinfo (g_hostname, NULL, &hints, &res);
	if (0 != e) {
		g_set_error (error,
			     PGM_HTTP_ERROR,
			     pgm_http_error_from_eai_errno (e),
			     _("Resolving hostname address: %s"),
			     gai_strerror (e));
		return FALSE;
	}
	e = getnameinfo (res->ai_addr, pgm_sockaddr_len (res->ai_addr),
		         g_address, sizeof(g_address),
			 NULL, 0,
			 NI_NUMERICHOST);
	if (0 != e) {
		g_set_error (error,
			     PGM_HTTP_ERROR,
			     pgm_http_error_from_eai_errno (e),
			     _("Resolving numeric hostname: %s"),
			     gai_strerror (e));
		return FALSE;
	}
	freeaddrinfo (res);
	e = getlogin_r (g_username, sizeof(g_username));
	if (0 != e) {
		g_set_error (error,
			     PGM_HTTP_ERROR,
			     pgm_http_error_from_errno (errno),
			     _("Retrieving user name: %s"),
			     g_strerror (errno));
		return FALSE;
	}
	g_pid = getpid();

	g_thread_mutex	= g_mutex_new();
	g_thread_cond	= g_cond_new();

	GThread* thread = g_thread_create_full (http_thread,
						(gpointer)&http_port,
						0,		/* stack size */
						TRUE,		/* joinable */
						TRUE,		/* native thread */
						G_THREAD_PRIORITY_LOW,	/* lowest */
						error);
	if (!thread) {
		g_prefix_error (error,
				_("Creating HTTP thread: "));
		g_cond_free  (g_thread_cond);
		g_mutex_free (g_thread_mutex);
		return FALSE;
	}

	g_thread = thread;

/* spin lock around condition waiting for thread startup */
	g_mutex_lock (g_thread_mutex);
	while (!g_soup_server)
		g_cond_wait (g_thread_cond, g_thread_mutex);
	g_mutex_unlock (g_thread_mutex);

/* catch failure */
	if (NULL == g_soup_server) {
		g_propagate_error (error, g_error);
		g_cond_free  (g_thread_cond);
		g_mutex_free (g_thread_mutex);
		return FALSE;
	}

/* cleanup */
	g_cond_free  (g_thread_cond);
	g_mutex_free (g_thread_mutex);
	return TRUE;
}

gboolean
pgm_http_shutdown (void)
{
	g_return_val_if_fail (NULL != g_soup_server, FALSE);
	soup_server_quit (g_soup_server);
	g_thread_join (g_thread);
	g_thread = NULL; g_soup_server = NULL;
	return TRUE;
}

static
gpointer
http_thread (
	gpointer		data
	)
{
	g_assert (NULL != data);

	const guint16 http_port = *(guint16*)data;
	GMainContext* context = g_main_context_new ();
	g_mutex_lock (g_thread_mutex);
	g_soup_server = soup_server_new (SOUP_SERVER_PORT, http_port,
					 SOUP_SERVER_ASYNC_CONTEXT, context,
					 NULL);
	if (!g_soup_server) {
		g_set_error (&g_error,
			     PGM_HTTP_ERROR,
			     PGM_HTTP_ERROR_FAILED,
			     _("Creating new Soup Server: %s"),
			     g_strerror (errno));
		g_main_context_unref (context);
		g_cond_signal  (g_thread_cond);
		g_mutex_unlock (g_thread_mutex);
		return NULL;
	}

	g_message ("web interface: http://%s:%i",
			g_hostname,
			soup_server_get_port (g_soup_server));

#ifdef CONFIG_LIBSOUP22
	soup_server_add_handler (g_soup_server, NULL,		NULL, default_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/robots.txt",	NULL, robots_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/base.css",	NULL, css_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/",		NULL, index_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/interfaces",	NULL, interfaces_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/transports",	NULL, transports_callback, NULL, NULL);
#ifdef CONFIG_HISTOGRAMS
	soup_server_add_handler (g_soup_server, "/histograms",	NULL, histograms_callback, NULL, NULL);
#endif /* CONFIG_HISTOGRAMS */
#else /* !CONFIG_LIBSOUP22 */
	soup_server_add_handler (g_soup_server, NULL,		default_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/robots.txt",	robots_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/base.css",	css_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/",		index_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/interfaces",	interfaces_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/transports",	transports_callback, NULL, NULL);
#ifdef CONFIG_HISTOGRAMS
	soup_server_add_handler (g_soup_server, "/histograms",	histograms_callback, NULL, NULL);
#endif /* CONFIG_HISTOGRAMS */
#endif /* !CONFIG_LIBSOUP22 */

/* signal parent thread we are ready to run */
	g_cond_signal  (g_thread_cond);
	g_mutex_unlock (g_thread_mutex);
/* main loop */
	soup_server_run (g_soup_server);
/* cleanup */
	g_object_unref (g_soup_server);
	g_main_context_unref (context);
	return NULL;
}

/* add xhtml doctype and head, populate with runtime values
 */

typedef enum {
	HTTP_TAB_GENERAL_INFORMATION,
	HTTP_TAB_INTERFACES,
	HTTP_TAB_TRANSPORTS,
	HTTP_TAB_HISTOGRAMS
} http_tab_e;

static
GString*
http_create_response (
	const gchar*		subtitle,
	http_tab_e		tab
	)
{
	g_assert (NULL != subtitle);
	g_assert (tab == HTTP_TAB_GENERAL_INFORMATION ||
		  tab == HTTP_TAB_INTERFACES ||
		  tab == HTTP_TAB_TRANSPORTS ||
		  tab == HTTP_TAB_HISTOGRAMS);

/* surprising deficiency of GLib is no support of display locale time */
	char buf[100];
	const time_t nowdate = time(NULL);
	struct tm now;
	localtime_r (&nowdate, &now);
	gsize ret = strftime (buf, sizeof(buf), "%c", &now);
	gsize bytes_written;
	gchar* timestamp = g_locale_to_utf8 (buf, ret, NULL, &bytes_written, NULL);

	GString* response = g_string_new (WWW_XHTML10_STRICT_DOCTYPE);
	g_string_append_printf (response, "\n<head>"
						"<title>%s - %s</title>"
						"<link rel=\"stylesheet\" href=\"/base.css\" type=\"text/css\" charset=\"utf-8\" />"
					"</head>\n"
					"<body>"
					"<div id=\"header\">"
						"<span id=\"hostname\">%s</span>"
						" | <span id=\"banner\"><a href=\"http://code.google.com/p/openpgm/\">OpenPGM</a> %u.%u.%u</span>"
						" | <span id=\"timestamp\">%s</span>"
					"</div>"
					"<div id=\"navigation\">"
						"<a href=\"/\"><span class=\"tab\" id=\"tab%s\">General Information</span></a>"
						"<a href=\"/interfaces\"><span class=\"tab\" id=\"tab%s\">Interfaces</span></a>"
						"<a href=\"/transports\"><span class=\"tab\" id=\"tab%s\">Transports</span></a>"
#ifdef CONFIG_HISTOGRAMS
						"<a href=\"/histograms\"><span class=\"tab\" id=\"tab%s\">Histograms</span></a>"
#endif
						"<div id=\"tabline\"></div>"
					"</div>"
					"<div id=\"content\">",
				g_hostname,
				subtitle,
				g_hostname,
				pgm_major_version, pgm_minor_version, pgm_micro_version,
				timestamp,
				tab == HTTP_TAB_GENERAL_INFORMATION ? "top" : "bottom",
				tab == HTTP_TAB_INTERFACES ? "top" : "bottom",
				tab == HTTP_TAB_TRANSPORTS ? "top" : "bottom"
#ifdef CONFIG_HISTOGRAMS
				,tab == HTTP_TAB_HISTOGRAMS ? "top" : "bottom"
#endif
	);

	g_free (timestamp);
	return response;
}

static
void
http_finalize_response (
	GString*		response,
	SoupMessage*		msg
	)
{
	g_string_append (response,	"</div>"
					"<div id=\"footer\">"
						"&copy;2009 Miru"
					"</div>"
					"</body>\n"
					"</html>");

	gchar* buf = g_string_free (response, FALSE);
	soup_message_set_status (msg, SOUP_STATUS_OK);
#ifdef CONFIG_LIBSOUP22
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_SYSTEM_OWNED,
					buf, strlen(buf));
#else
	soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_TAKE,
					buf, strlen(buf));
#endif
}

#ifdef CONFIG_LIBSOUP22	
static
void
robots_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	G_GNUC_UNUSED gpointer	data
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
#else
static
void
robots_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
{
	if (0 != g_strcmp0 (msg->method, "GET")) {
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
		return;
	}

	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CONTENT_LENGTH);
        soup_message_set_response (msg, "text/plain", SOUP_MEMORY_STATIC,
                                WWW_ROBOTS_TXT, strlen(WWW_ROBOTS_TXT));
}
#endif

#ifdef CONFIG_LIBSOUP22 
static void
css_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	G_GNUC_UNUSED gpointer	data
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
#else
static void
css_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
{       
        if (0 != g_strcmp0 (msg->method, "GET")) {
                soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
                return;
        }

        soup_message_set_status (msg, SOUP_STATUS_OK);
        soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CONTENT_LENGTH);
        soup_message_set_response (msg, "text/css", SOUP_MEMORY_STATIC,
                                WWW_BASE_CSS, strlen(WWW_BASE_CSS));
}
#endif

#ifdef CONFIG_LIBSOUP22
static void
index_callback (
	G_GNUC_UNUSED SoupServerContext* context,
	SoupMessage*			msg,
	G_GNUC_UNUSED gpointer		data
	)
{
#else
static void
index_callback (
        SoupServer*		server,
        SoupMessage*    	msg,
        const char*       	path,
        GHashTable* 		query,
        SoupClientContext*	client,
        gpointer 		data
        )
{
	if (strlen (path) > 1) {
		default_callback (server, msg, path, query, client, data);
		return;
	}
#endif
	g_static_rw_lock_reader_lock (&pgm_transport_list_lock);
	const int transport_count = g_slist_length (pgm_transport_list);
	g_static_rw_lock_reader_unlock (&pgm_transport_list_lock);

	GString* response = http_create_response ("OpenPGM", HTTP_TAB_GENERAL_INFORMATION);
	g_string_append_printf (response,	"<table>"
						"<tr>"
							"<th>host name:</th><td>%s</td>"
						"</tr><tr>"
							"<th>user name:</th><td>%s</td>"
						"</tr><tr>"
							"<th>IP address:</th><td>%s</td>"
						"</tr><tr>"
							"<th>transports:</th><td><a href=\"/transports\">%i</a></td>"
						"</tr><tr>"
							"<th>process ID:</th><td>%i</td>"
						"</tr>"
						"</table>\n",
				g_hostname,
				g_username,
				g_address,
				transport_count,
				g_pid);

	http_finalize_response (response, msg);
}

#ifdef CONFIG_LIBSOUP22
static void
interfaces_callback (
	G_GNUC_UNUSED SoupServerContext* context,
	SoupMessage*			msg,
	G_GNUC_UNUSED gpointer		data
	)
#else
static void
interfaces_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
#endif
{
	GString* response = http_create_response ("Interfaces", HTTP_TAB_INTERFACES);
	g_string_append (response, "<PRE>");
	struct ifaddrs *ifap, *ifa;
	int e = getifaddrs (&ifap);
	if (e < 0) {
		g_string_append_printf (response, "getifaddrs(): %s", g_strerror (errno));
		http_finalize_response (response, msg);
		return;
	}
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		int i = NULL == ifa->ifa_addr ? 0 : pgm_if_nametoindex (ifa->ifa_addr->sa_family, ifa->ifa_name);
		char rname[IF_NAMESIZE * 2 + 3];
		char b[IF_NAMESIZE * 2 + 3];

		if_indextoname(i, rname);
		sprintf (b, "%s (%s)", ifa->ifa_name, rname);

		 if (NULL == ifa->ifa_addr ||
		      (ifa->ifa_addr->sa_family != AF_INET &&
		       ifa->ifa_addr->sa_family != AF_INET6) )
		{
			g_string_append_printf (response,
				"#%d name %-15.15s ---- %-46.46s scope 0 status %s loop %s b/c %s m/c %s<BR/>\n",
				i,
				b,
				"",
				ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
				ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
				ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
				ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
			continue;
		}

		char s[INET6_ADDRSTRLEN];
		getnameinfo (ifa->ifa_addr, pgm_sockaddr_len(ifa->ifa_addr),
			     s, sizeof(s),
			     NULL, 0,
			     NI_NUMERICHOST);
		g_string_append_printf (response,
			"#%d name %-15.15s IPv%i %-46.46s scope %u status %s loop %s b/c %s m/c %s<BR/>\n",
			i,
			b,
			ifa->ifa_addr->sa_family == AF_INET ? 4 : 6,
			s,
			(unsigned)pgm_sockaddr_scope_id(ifa->ifa_addr),
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
		);
	}
	freeifaddrs (ifap);
	g_string_append (response, "</PRE>\n");
	http_finalize_response (response, msg);
}

#ifdef CONFIG_LIBSOUP22
static void
transports_callback (
	G_GNUC_UNUSED SoupServerContext* context,
	SoupMessage*			msg,
	G_GNUC_UNUSED gpointer		data
	)
#else
static void
transports_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
#endif
{
	GString* response = http_create_response ("Transports", HTTP_TAB_TRANSPORTS);
	g_string_append (response,	"<div class=\"bubbly\">"
					"\n<table cellspacing=\"0\">"
					"<tr>"
						"<th>Group address</th>"
						"<th>Dest port</th>"
						"<th>Source GSI</th>"
						"<th>Source port</th>"
					"</tr>"
				);

	if (pgm_transport_list)
	{
		g_static_rw_lock_reader_lock (&pgm_transport_list_lock);

		GSList* list = pgm_transport_list;
		while (list)
		{
			GSList* next = list->next;
			pgm_transport_t* transport = list->data;

			char group_address[INET6_ADDRSTRLEN];
			getnameinfo ((struct sockaddr*)&transport->send_gsr.gsr_group, pgm_sockaddr_len (&transport->send_gsr.gsr_group),
				     group_address, sizeof(group_address),
				     NULL, 0,
				     NI_NUMERICHOST);
			char gsi[sizeof("000.000.000.000.000.000")];
			snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
				transport->tsi.gsi.identifier[0],
				transport->tsi.gsi.identifier[1],
				transport->tsi.gsi.identifier[2],
				transport->tsi.gsi.identifier[3],
				transport->tsi.gsi.identifier[4],
				transport->tsi.gsi.identifier[5]);
			const int sport = g_ntohs (transport->tsi.sport);
			const int dport = g_ntohs (transport->dport);
			g_string_append_printf (response,	"<tr>"
									"<td>%s</td>"
									"<td>%i</td>"
									"<td><a href=\"/%s.%hu\">%s</a></td>"
									"<td><a href=\"/%s.%hu\">%hu</a></td>"
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
	}
	else
	{
/* no transports */
		g_string_append (response,		"<tr>"
							"<td colspan=\"6\"><div class=\"empty\">This transport has no peers.</div></td>"
							"</tr>"
				);
	}

	g_string_append (response,		"</table>\n"
						"</div>");
	http_finalize_response (response, msg);
}

#ifdef CONFIG_LIBSOUP22
static void
histograms_callback (
	G_GNUC_UNUSED SoupServerContext* context,
	SoupMessage*			msg,
	G_GNUC_UNUSED gpointer		data
	)
#else
static void
histograms_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
#endif
{
	GString* response = http_create_response ("Histograms", HTTP_TAB_HISTOGRAMS);
	pgm_histogram_write_html_graph_all (response);
	http_finalize_response (response, msg);
}

#ifdef CONFIG_LIBSOUP22
static void
default_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	G_GNUC_UNUSED gpointer	data
	)
{
	if (context->method_id != SOUP_METHOD_ID_GET) {
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
		return;
	}

/* magical mysterious GSI hunting from path */
	char *path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);
#else
static void
default_callback (
        G_GNUC_UNUSED SoupServer*       server,
        SoupMessage*    		msg,
        G_GNUC_UNUSED const char*       path,
        G_GNUC_UNUSED GHashTable* 	query,
        G_GNUC_UNUSED SoupClientContext* client,
        G_GNUC_UNUSED gpointer 		data
        )
{
        if (0 != g_strcmp0 (msg->method, "GET")) {
                soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
                return;
        }
#endif

	pgm_tsi_t tsi;
	const int count = sscanf (path, "/%hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hu",
				(unsigned char*)&tsi.gsi.identifier[0],
				(unsigned char*)&tsi.gsi.identifier[1],
				(unsigned char*)&tsi.gsi.identifier[2],
				(unsigned char*)&tsi.gsi.identifier[3],
				(unsigned char*)&tsi.gsi.identifier[4],
				(unsigned char*)&tsi.gsi.identifier[5],
				&tsi.sport);
	tsi.sport = g_htons (tsi.sport);
#ifdef CONFIG_LIBSOUP22
	g_free (path);
#endif
	if (count == 7)
	{
		int retval = http_tsi_response (&tsi, msg);
		if (!retval) return;
	}

	soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
#ifdef CONFIG_LIBSOUP22
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg), SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
					WWW_404_HTML, strlen(WWW_404_HTML));
#else
	soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_STATIC,
					WWW_404_HTML, strlen(WWW_404_HTML));
#endif
}

static int
http_tsi_response (
	pgm_tsi_t*		tsi,
	SoupMessage*		msg
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
	getnameinfo ((struct sockaddr*)&transport->send_gsr.gsr_source, pgm_sockaddr_len (&transport->send_gsr.gsr_source),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&transport->send_gsr.gsr_group, pgm_sockaddr_len (&transport->send_gsr.gsr_group),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	const int dport = g_ntohs (transport->dport);
	const int sport = g_ntohs (transport->tsi.sport);

	pgm_time_t ihb_min = transport->spm_heartbeat_len ? transport->spm_heartbeat_interval[ 1 ] : 0;
	pgm_time_t ihb_max = transport->spm_heartbeat_len ? transport->spm_heartbeat_interval[ transport->spm_heartbeat_len - 1 ] : 0;

	char spm_path[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&transport->recv_gsr[0].gsr_source, pgm_sockaddr_len (&transport->recv_gsr[0].gsr_source),
		     spm_path, sizeof(spm_path),
		     NULL, 0,
		     NI_NUMERICHOST);

	GString* response = http_create_response (title, HTTP_TAB_TRANSPORTS);
	g_string_append_printf (response,	"<div class=\"heading\">"
							"<strong>Transport: </strong>"
							"%s.%i"
						"</div>",
				gsi, sport);

/* peers */

	g_string_append (response,		"<div class=\"bubbly\">"
						"\n<table cellspacing=\"0\">"
						"<tr>"
							"<th>Group address</th>"
							"<th>Dest port</th>"
							"<th>Source address</th>"
							"<th>Last hop</th>"
							"<th>Source GSI</th>"
							"<th>Source port</th>"
						"</tr>"
			);

	if (transport->peers_list)
	{
		g_static_rw_lock_reader_lock (&transport->peers_lock);
		GList* peers_list = transport->peers_list;
		while (peers_list) {
			GList* next = peers_list->next;
			http_each_receiver (peers_list->data, response);
			peers_list = next;
		}
		g_static_rw_lock_reader_unlock (&transport->peers_lock);
	}
	else
	{
/* no peers */

		g_string_append (response,	"<tr>"
							"<td colspan=\"6\"><div class=\"empty\">This transport has no peers.</div></td>"
						"</tr>"
				);

	}

	g_string_append (response,		"</table>\n"
						"</div>");

/* source and configuration information */

	g_string_append_printf (response,	"<div class=\"rounded\" id=\"information\">"
						"\n<table>"
						"<tr>"
							"<th>Source address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Group address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Dest port</th><td>%i</td>"
						"</tr><tr>"
							"<th>Source GSI</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source port</th><td>%i</td>"
						"</tr>",
				source_address,
				group_address,
				dport,
				gsi,
				sport);

/* continue with source information */

	g_string_append_printf (response,	"<tr>"
							"<td colspan=\"2\"><div class=\"break\"></div></td>"
						"</tr><tr>"
							"<th>Ttl</th><td>%u</td>"
						"</tr><tr>"
							"<th>Adv Mode</th><td>data(1)</td>"
						"</tr><tr>"
							"<th>Late join</th><td>disable(2)</td>"
						"</tr><tr>"
							"<th>TXW_MAX_RTE</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>TXW_SECS</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>TXW_ADV_SECS</th><td>0</td>"
						"</tr><tr>"
							"<th>Ambient SPM interval</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>IHB_MIN</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>IHB_MAX</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_BO_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>FEC</th><td>disabled(1)</td>"
						"</tr><tr>"
							"<th>Source Path Address</th><td>%s</td>"
						"</tr>"
						"</table>\n"
						"</div>",
				transport->hops,
				transport->txw_max_rte,
				transport->txw_secs,
				pgm_to_msecs(transport->spm_ambient_interval),
				ihb_min,
				ihb_max,
				pgm_to_msecs(transport->nak_bo_ivl),
				spm_path);

/* performance information */

	g_string_append_printf (response,	"\n<h2>Performance information</h2>"
						"\n<table>"
						"<tr>"
							"<th>Data bytes sent</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Data packets sent</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Bytes buffered</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>Packets buffered</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>Bytes sent</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Raw NAKs received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Checksum errors</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed NAKs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Packets discarded</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Bytes retransmitted</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Packets retransmitted</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs ignored</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Transmission rate</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " bps</td>"
						"</tr><tr>"
							"<th>NNAK packets received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NNAKs received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed NNAKs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr>"
						"</table>\n",
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT],
						transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT],
						transport->window ? pgm_txw_size((pgm_txw_t*)transport->window) : 0,	/* minus IP & any UDP header */
						transport->window ? pgm_txw_length((pgm_txw_t*)transport->window) : 0,
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
	pgm_peer_t*		peer,
	GString*		response
	)
{
	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->group_nla, pgm_sockaddr_len (&peer->group_nla),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char source_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->nla, pgm_sockaddr_len (&peer->nla),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char last_hop[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->local_nla, pgm_sockaddr_len (&peer->local_nla),
		     last_hop, sizeof(last_hop),
		     NULL, 0,
		     NI_NUMERICHOST);

	char gsi[sizeof("000.000.000.000.000.000")];
	snprintf(gsi, sizeof(gsi), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
			peer->tsi.gsi.identifier[0],
			peer->tsi.gsi.identifier[1],
			peer->tsi.gsi.identifier[2],
			peer->tsi.gsi.identifier[3],
			peer->tsi.gsi.identifier[4],
			peer->tsi.gsi.identifier[5]);

	const int sport = g_ntohs (peer->tsi.sport);
	const int dport = g_ntohs (peer->transport->dport);	/* by definition must be the same */
	g_string_append_printf (response,	"<tr>"
							"<td>%s</td>"
							"<td>%i</td>"
							"<td>%s</td>"
							"<td>%s</td>"
							"<td><a href=\"/%s.%i\">%s</a></td>"
							"<td><a href=\"/%s.%i\">%i</a></td>"
						"</tr>",
				group_address,
				dport,
				source_address,
				last_hop,
				gsi, sport, gsi,
				gsi, sport, sport
			);
}

static int
http_time_summary (
	const time_t* 		activity_time,
	char* 			sz
	)
{
	time_t now_time = time(NULL);

	if (*activity_time > now_time) {
		return sprintf (sz, "clock skew");
	}

	struct tm activity_tm;
	localtime_r (activity_time, &activity_tm);

	now_time -= *activity_time;

	if (now_time < (24 * 60 * 60))
	{
		char hourmin[6];
		strftime (hourmin, sizeof(hourmin), "%H:%M", &activity_tm);

		if (now_time < 60) {
			return sprintf (sz, "%s (%li second%s ago)",
					hourmin, now_time, now_time > 1 ? "s" : "");
		}
		now_time /= 60;
		if (now_time < 60) {
			return sprintf (sz, "%s (%li minute%s ago)",
					hourmin, now_time, now_time > 1 ? "s" : "");
		}
		now_time /= 60;
		return sprintf (sz, "%s (%li hour%s ago)",
				hourmin, now_time, now_time > 1 ? "s" : "");
	}
	else
	{
		char daymonth[32];
		strftime (daymonth, sizeof(daymonth), "%d %b", &activity_tm);
		now_time /= 24;
		if (now_time < 14) {
			return sprintf (sz, "%s (%li day%s ago)",
					daymonth, now_time, now_time > 1 ? "s" : "");
		} else {
			return sprintf (sz, "%s", daymonth);
		}
	}
}

static int
http_receiver_response (
	pgm_peer_t*		peer,
	SoupMessage*		msg
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

	char title[ sizeof("Peer 000.000.000.000.000.000.00000") ];
	sprintf (title, "Peer %s.%hu",
		gsi,
		g_ntohs (peer->tsi.sport));

	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->group_nla, pgm_sockaddr_len (&peer->group_nla),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char source_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->nla, pgm_sockaddr_len (&peer->nla),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char last_hop[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&peer->local_nla, pgm_sockaddr_len (&peer->local_nla),
		     last_hop, sizeof(last_hop),
		     NULL, 0,
		     NI_NUMERICHOST);

	const int sport = g_ntohs (peer->tsi.sport);
	const int dport = g_ntohs (peer->transport->dport);	/* by definition must be the same */
	const guint32 outstanding_naks = ((pgm_rxw_t*)peer->window)->backoff_queue.length +
					 ((pgm_rxw_t*)peer->window)->wait_ncf_queue.length +
					 ((pgm_rxw_t*)peer->window)->wait_data_queue.length;

	time_t last_activity_time;
	pgm_time_since_epoch (&peer->last_packet, &last_activity_time);

	char buf[100];
	http_time_summary (&last_activity_time, buf);

	gsize bytes_written;
	gchar* last_activity = g_locale_to_utf8 (buf, strlen(buf), NULL, &bytes_written, NULL);

	GString* response = http_create_response (title, HTTP_TAB_TRANSPORTS);
	g_string_append_printf (response,	"<div class=\"heading\">"
							"<strong>Peer: </strong>"
							"%s.%i"
						"</div>",
				gsi, sport);


/* peer information */
	g_string_append_printf (response,	"<div class=\"rounded\" id=\"information\">"
						"\n<table>"
						"<tr>"
							"<th>Group address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Dest port</th><td>%i</td>"
						"</tr><tr>"
							"<th>Source address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Last hop</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source GSI</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source port</th><td>%i</td>"
						"</tr>",
				group_address,
				dport,
				source_address,
				last_hop,
				gsi,
				sport);

	g_string_append_printf (response,	"<tr>"
							"<td colspan=\"2\"><div class=\"break\"></div></td>"
						"</tr><tr>"
							"<th>NAK_BO_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_RPT_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_NCF_RETRIES</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>NAK_RDATA_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_DATA_RETRIES</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>Send NAKs</th><td>enabled(1)</td>"
						"</tr><tr>"
							"<th>Late join</th><td>disabled(2)</td>"
						"</tr><tr>"
							"<th>NAK TTL</th><td>%u</td>"
						"</tr><tr>"
							"<th>Delivery order</th><td>ordered(2)</td>"
						"</tr><tr>"
							"<th>Multicast NAKs</th><td>disabled(2)</td>"
						"</tr>"
						"</table>\n"
						"</div>",
						pgm_to_msecs(peer->transport->nak_bo_ivl),
						pgm_to_msecs(peer->transport->nak_rpt_ivl),
						peer->transport->nak_ncf_retries,
						pgm_to_msecs(peer->transport->nak_rdata_ivl),
						peer->transport->nak_data_retries,
						peer->transport->hops);

	g_string_append_printf (response,	"\n<h2>Performance information</h2>"
						"\n<table>"
						"<tr>"
							"<th>Data bytes received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Data packets received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAK failures</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Bytes received</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Checksum errors</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed SPMs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed ODATA</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed RDATA</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed NCFs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Packets discarded</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Losses</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"	/* detected missed packets */
						"</tr><tr>"
							"<th>Bytes delivered to app</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Packets delivered to app</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Duplicate SPMs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Duplicate ODATA/RDATA</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAK packets sent</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs sent</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs retransmitted</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs failed</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to RXW advance</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to NCF retries</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to DATA retries</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAK failures delivered to app</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAKs suppressed</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Malformed NAKs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Outstanding NAKs</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>Last activity</th><td>%s</td>"
						"</tr><tr>"
							"<th>NAK repair min time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK repair mean time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK repair max time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK fail min time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK fail mean time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK fail max time</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT " μs</td>"
						"</tr><tr>"
							"<th>NAK min retransmit count</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAK mean retransmit count</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr><tr>"
							"<th>NAK max retransmit count</th><td>%" GROUP_FORMAT G_GUINT32_FORMAT "</td>"
						"</tr>"
						"</table>\n",
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAILURES],
						peer->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED],
						peer->transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_RDATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS],
						peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED],
						((pgm_rxw_t*)peer->window)->cumulative_losses,
						((pgm_rxw_t*)peer->window)->bytes_delivered,
						((pgm_rxw_t*)peer->window)->msgs_delivered,
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
						((pgm_rxw_t*)peer->window)->min_fill_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_SVC_TIME_MEAN],
						((pgm_rxw_t*)peer->window)->max_fill_time,
						peer->min_fail_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAIL_TIME_MEAN],
						peer->max_fail_time,
						((pgm_rxw_t*)peer->window)->min_nak_transmit_count,
						peer->cumulative_stats[PGM_PC_RECEIVER_TRANSMIT_MEAN],
						((pgm_rxw_t*)peer->window)->max_nak_transmit_count);
	http_finalize_response (response, msg);
	g_free (last_activity);
	return 0;
}

GQuark
pgm_http_error_quark (void)
{
	return g_quark_from_static_string ("pgm-http-error-quark");
}

static
PGMHTTPError
pgm_http_error_from_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EFAULT
	case EFAULT:
		return PGM_HTTP_ERROR_FAULT;
		break;
#endif

#ifdef EINVAL
	case EINVAL:
		return PGM_HTTP_ERROR_INVAL;
		break;
#endif

#ifdef EPERM
	case EPERM:
		return PGM_HTTP_ERROR_PERM;
		break;
#endif

#ifdef EMFILE
	case EMFILE:
		return PGM_HTTP_ERROR_MFILE;
		break;
#endif

#ifdef ENFILE
	case ENFILE:
		return PGM_HTTP_ERROR_NFILE;
		break;
#endif

#ifdef ENXIO
	case ENXIO:
		return PGM_HTTP_ERROR_NXIO;
		break;
#endif

#ifdef ERANGE
	case ERANGE:
		return PGM_HTTP_ERROR_RANGE;
		break;
#endif

#ifdef ENOENT
	case ENOENT:
		return PGM_HTTP_ERROR_NOENT;
		break;
#endif

	default :
		return PGM_HTTP_ERROR_FAILED;
		break;
	}
}

/* errno must be preserved before calling to catch correct error
 * status with EAI_SYSTEM.
 */

static
PGMHTTPError
pgm_http_error_from_eai_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return PGM_HTTP_ERROR_ADDRFAMILY;
		break;
#endif

#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return PGM_HTTP_ERROR_AGAIN;
		break;
#endif

#ifdef EAI_BADFLAGS
	case EAI_BADFLAGS:
		return PGM_HTTP_ERROR_BADFLAGS;
		break;
#endif

#ifdef EAI_FAIL
	case EAI_FAIL:
		return PGM_HTTP_ERROR_FAIL;
		break;
#endif

#ifdef EAI_FAMILY
	case EAI_FAMILY:
		return PGM_HTTP_ERROR_FAMILY;
		break;
#endif

#ifdef EAI_MEMORY
	case EAI_MEMORY:
		return PGM_HTTP_ERROR_MEMORY;
		break;
#endif

#ifdef EAI_NODATA
	case EAI_NODATA:
		return PGM_HTTP_ERROR_NODATA;
		break;
#endif

#ifdef EAI_NONAME
	case EAI_NONAME:
		return PGM_HTTP_ERROR_NONAME;
		break;
#endif

#ifdef EAI_SERVICE
	case EAI_SERVICE:
		return PGM_HTTP_ERROR_SERVICE;
		break;
#endif

#ifdef EAI_SOCKTYPE
	case EAI_SOCKTYPE:
		return PGM_HTTP_ERROR_SOCKTYPE;
		break;
#endif

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return pgm_http_error_from_errno (errno);
		break;
#endif

	default :
		return PGM_HTTP_ERROR_FAILED;
		break;
	}
}

/* eof */

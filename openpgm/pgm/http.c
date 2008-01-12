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
#include <unistd.h>

#include <glib.h>

#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-server-message.h>
#include <libsoup/soup-address.h>

#include <pgm/http.h>

#include "htdocs/404.html.h"
#include "htdocs/base.css.h"
#include "htdocs/robots.txt.h"


/* globals */

/* local globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN            "pgmhttp"

static guint16 g_server_port;

static GThread* thread;
static SoupServer* g_soup_server = NULL;
static GCond* http_cond;
static GMutex* http_mutex;

static gpointer http_thread(gpointer);

static void default_callback (SoupServerContext*, SoupMessage*, gpointer);
static void robots_callback (SoupServerContext*, SoupMessage*, gpointer);
static void css_callback (SoupServerContext*, SoupMessage*, gpointer);
static void index_callback (SoupServerContext*, SoupMessage*, gpointer);


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
		g_warning ("soup server failed startup: %s\n", strerror (errno));
		goto out;
	}

	char hostname[NI_MAXHOST + 1];
	gethostname (hostname, sizeof(hostname));

	g_message ("web interface: http://%s:%i\n",
			hostname,
			soup_server_get_port (g_soup_server));

	soup_server_add_handler (g_soup_server, NULL,   NULL, default_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/robots.txt",  NULL, robots_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/main.css",    NULL, css_callback, NULL, NULL);
	soup_server_add_handler (g_soup_server, "/",    NULL, index_callback, NULL, NULL);

/* signal parent thread we are ready to run */
	g_cond_signal (http_cond);
	g_mutex_unlock (http_mutex);

	soup_server_run (g_soup_server);
	g_object_unref (g_soup_server);

out:
	g_main_context_unref (context);
	return NULL;
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

	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg),
						SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
					WWW_404_HTML, strlen(WWW_404_HTML));
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
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg),
						SOUP_TRANSFER_CONTENT_LENGTH);
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
	soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg),
						SOUP_TRANSFER_CONTENT_LENGTH);
	soup_message_set_response (msg, "text/plain", SOUP_BUFFER_STATIC,
					WWW_BASE_CSS, strlen(WWW_BASE_CSS));
}

static void
index_callback (
	SoupServerContext*	context,
	SoupMessage*		msg,
	gpointer		data
		)
{
	return default_callback (context, msg, data);
}

/* eof */

/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Sit in glib event loop serving basic HTTP.
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
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>


#include <glib.h>
#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-address.h>

#include "pgm/backtrace.h"
#include "pgm/log.h"


/* globals */

#define WWW_NOTFOUND	"<html><head><title>404</title></head><body>lah, 404 :)</body></html>\r\n"


static int g_port = SOUP_ADDRESS_ANY_PORT;

static GMainLoop* g_loop = NULL;
static SoupServer* g_soup_server = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void default_callback (SoupServerContext*, SoupMessage*, gpointer);


G_GNUC_NORETURN static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -p <port>       : IP port for web interface\n");
	exit (1);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	puts ("basic_http");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "p:h")) != -1)
	{
		switch (c) {
		case 'p':	g_port = atoi (optarg); break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	log_init ();

	g_type_init ();
	g_thread_init (NULL);

/* setup signal handlers */
	signal(SIGSEGV, on_sigsegv);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, SIG_IGN);

/* delayed startup */
	puts ("scheduling startup.");
	g_timeout_add(0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_loop = g_main_loop_new(NULL, FALSE);

	puts ("entering main event loop ... ");
	g_main_loop_run(g_loop);

	puts ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref(g_loop);
	g_loop = NULL;

	if (g_soup_server) {
		g_object_unref (g_soup_server);
		g_soup_server = NULL;
	}

	puts ("finished.");
	return 0;
}

static void
on_signal (
	G_GNUC_UNUSED int signum
	)
{
	puts ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	puts ("startup.");

	puts ("starting soup server.");
	g_soup_server = soup_server_new (SOUP_SERVER_PORT, g_port,
					NULL);
	if (!g_soup_server) {
		printf ("soup server failed: %s\n", strerror (errno));
		g_main_loop_quit (g_loop);
		return FALSE;
	}

	char hostname[NI_MAXHOST + 1];
        gethostname (hostname, sizeof(hostname));

	printf ("web interface: http://%s:%i\n", 
		hostname,
		soup_server_get_port (g_soup_server));

	soup_server_add_handler (g_soup_server, NULL, NULL, 
				default_callback, NULL, NULL);

	soup_server_run_async (g_soup_server);
	g_object_unref (g_soup_server);

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	puts ("startup complete.");
	return FALSE;
}


static gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("-- MARK --");
	return TRUE;
}

/* request on web interface
 */

static void
default_callback (
	G_GNUC_UNUSED SoupServerContext* context,
	SoupMessage*		msg,
	G_GNUC_UNUSED gpointer	data
		)
{
	char *path;

	path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);
	printf ("%s %s HTTP/1.%d\n", msg->method, path,
		soup_message_get_http_version (msg));

	soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
					WWW_NOTFOUND, strlen(WWW_NOTFOUND));
	soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
	soup_message_add_header (msg->response_headers, "Connection", "close");
}

/* eof */

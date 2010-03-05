/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * SNMP
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
#include <fcntl.h>
#include <unistd.h>

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "pgm/snmp.h"
#include "pgm/pgmMIB.h"
#include "pgm/notify.h"


/* globals */

gboolean	pgm_agentx_subagent = TRUE;
char*		pgm_agentx_socket = NULL;
gboolean	pgm_snmp_syslog = FALSE;
const char*	pgm_snmp_appname = "PGM";

static GThread*		g_thread = NULL;
static pgm_notify_t	g_thread_shutdown;

static gpointer agent_thread(gpointer);


static int
log_handler (
	G_GNUC_UNUSED netsnmp_log_handler*	logh,
	int			pri,
	const char*		string
	)
{
	GLogLevelFlags log_level = G_LOG_LEVEL_DEBUG;
	switch (pri) {
	case LOG_EMERG:
	case LOG_ALERT:
	case LOG_CRIT:		/*log_level = G_LOG_LEVEL_CRITICAL; break;*/	/* net-snmp 5.4.1 borken */
	case LOG_ERR:		/*log_level = G_LOG_LEVEL_ERROR; break;*/
	case LOG_WARNING:	log_level = G_LOG_LEVEL_WARNING; break;
	case LOG_NOTICE:	log_level = G_LOG_LEVEL_MESSAGE; break;
	case LOG_INFO:		log_level = G_LOG_LEVEL_INFO; break;
	}
	int len = strlen(string);
	if ( string[len - 1] == '\n' ) len--;
	char sbuf[1024];
	strncpy (sbuf, string, len);
	if (len > 0)
		sbuf[len - 1] = '\0';
	g_log (G_LOG_DOMAIN, log_level, "%s", sbuf);
	return 1;
}

gboolean
pgm_snmp_init (GError** error)
{
	g_return_val_if_fail (NULL == g_thread, FALSE);

/* ensure threading enabled */
	if (!g_thread_supported ()) g_thread_init (NULL);

/* redirect snmp logging */
	snmp_disable_log();
	netsnmp_log_handler* logh = netsnmp_register_loghandler (NETSNMP_LOGHANDLER_CALLBACK, LOG_DEBUG);
	logh->handler = log_handler;

	if (pgm_agentx_subagent)
	{
		if (pgm_agentx_socket)
		{
			netsnmp_ds_set_string (NETSNMP_DS_APPLICATION_ID,
						NETSNMP_DS_AGENT_X_SOCKET,
						pgm_agentx_socket);
		}
		netsnmp_ds_set_boolean (NETSNMP_DS_APPLICATION_ID,
					NETSNMP_DS_AGENT_ROLE,
					TRUE);
	}

/* initialise */
	if (0 != init_agent (pgm_snmp_appname)) {
		g_set_error (error,
			     PGM_SNMP_ERROR,
			     PGM_SNMP_ERROR_FAILED,
			     _("Initialise SNMP agent: see SNMP log for further details."));
		return FALSE;
	}

	if (!pgm_mib_init (error))
		return FALSE;

/* read config and parse mib */
	init_snmp (pgm_snmp_appname);

	if (!pgm_agentx_subagent)
	{
		if (0 != init_master_agent()) {
			g_set_error (error,
				     PGM_SNMP_ERROR,
				     PGM_SNMP_ERROR_FAILED,
				     _("Initialise SNMP master agent: see SNMP log for further details."));
			snmp_shutdown (pgm_snmp_appname);
			return FALSE;
		}
	}

/* create shutdown notification channel */
	if (0 != pgm_notify_init (&g_thread_shutdown)) {
		g_set_error (error,
			     PGM_SNMP_ERROR,
			     PGM_SNMP_ERROR_FAILED,
			     _("Creating SNMP shutdown notification channel: %s"),
			     g_strerror (errno));
		snmp_shutdown (pgm_snmp_appname);
		return FALSE;
	}

	GThread* thread = g_thread_create_full (agent_thread,
						NULL,
						0,		/* stack size */
						TRUE,		/* joinable */
						TRUE,		/* native thread */
						G_THREAD_PRIORITY_LOW,	/* lowest */
						error);
	if (!thread) {
		g_prefix_error (error,
				_("Creating SNMP thread: "));
		snmp_shutdown (pgm_snmp_appname);
		pgm_notify_destroy (&g_thread_shutdown);
		return FALSE;
	}

	g_thread = thread;
	return TRUE;
}

gboolean
pgm_snmp_shutdown (void)
{
	g_return_val_if_fail (NULL != g_thread, FALSE);

	pgm_notify_send (&g_thread_shutdown);
	g_thread_join (g_thread);
	g_thread = NULL;
	snmp_shutdown (pgm_snmp_appname);
	pgm_notify_destroy (&g_thread_shutdown);
	return TRUE;
}

static
gpointer
agent_thread (
	G_GNUC_UNUSED	gpointer	data
	)
{
	const int notify_fd = pgm_notify_get_fd (&g_thread_shutdown);
	for (;;)
	{
		int fds = 0, block = 1;
		fd_set fdset;
		struct timeval timeout;

		FD_ZERO(&fdset);
		snmp_select_info (&fds, &fdset, &timeout, &block);
		FD_SET(notify_fd, &fdset);
		if (notify_fd+1 > fds)
			fds = notify_fd+1;
		fds = select (fds, &fdset, NULL, NULL, block ? NULL : &timeout);
		if (FD_ISSET(notify_fd, &fdset))
			break;
		if (fds)
			snmp_read (&fdset);
		else
			snmp_timeout ();
	}

/* cleanup */
	return NULL;
}

GQuark
pgm_snmp_error_quark (void)
{
	return g_quark_from_static_string ("pgm-snmp-error-quark");
}


/* eof */

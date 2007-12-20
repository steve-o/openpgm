/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * SNMP
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


#include <glib.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "pgm/snmp.h"
#include "pgm/pgmMIB.h"


/* globals */

gboolean pgm_agentx_subagent = TRUE;
char* pgm_agentx_socket = NULL;
gboolean pgm_snmp_syslog = FALSE;
char* pgm_snmp_appname = "PGM";

static GThread*	thread;
static gboolean is_running = FALSE;

static gpointer agent_thread(gpointer);


int
pgm_snmp_init (void)
{
	int retval = 0;
	GError* err;
	GThread* tmp_thread;

/* ensure threading enabled */
	if (!g_thread_supported ()) g_thread_init (NULL);

	if (pgm_agentx_subagent)
	{
		netsnmp_enable_subagent();
		if (pgm_agentx_socket)
		{
			netsnmp_ds_set_string (NETSNMP_DS_APPLICATION_ID,
						NETSNMP_DS_AGENT_X_SOCKET,
						pgm_agentx_socket);
		}
	}

/* redirect logging */
	snmp_disable_log();
	if (pgm_snmp_syslog)
		snmp_enable_calllog();
	else
		snmp_enable_stderrlog();

/* initialise */
	retval = init_agent (pgm_snmp_appname);
	if (retval < 0) {
		g_error ("failed to initialise SNMP agent.");
		goto out;
	}

	pgm_mib_init ();

/* read config and parse mib */
	init_snmp (pgm_snmp_appname);

	if (!pgm_agentx_subagent)
	{
		retval = init_master_agent();
		if (retval < 0) {
			g_error ("failed to initialise SNMP master agent.");
			goto out;
		}
	}

	is_running = TRUE;
	tmp_thread = g_thread_create_full (agent_thread,
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

out:
	return retval;

err_destroy:
	return 0;
}

int
pgm_snmp_shutdown (void)
{
	is_running = FALSE;

	if (thread) {
		g_thread_join (thread);
		thread = NULL;
	}

	snmp_shutdown (pgm_snmp_appname);

	return 0;
}

static gpointer
agent_thread (
	gpointer	data
	)
{
	while (is_running) {
		agent_check_and_process (1);
	}

	return NULL;
}

/* eof */

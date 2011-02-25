/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * SNMP agent, single session.
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

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <errno.h>
#include <impl/i18n.h>
#include <impl/framework.h>

#include "pgm/snmp.h"
#include "impl/pgmMIB.h"


/* globals */

bool				pgm_agentx_subagent = TRUE;
char*				pgm_agentx_socket = NULL;
const char*			pgm_snmp_appname = "PGM";

/* locals */

#ifndef _WIN32
static pthread_t		snmp_thread;
static void*			snmp_routine (void*);
#else
static HANDLE			snmp_thread;
static unsigned __stdcall	snmp_routine (void*);
#endif
static pgm_notify_t		snmp_notify = PGM_NOTIFY_INIT;
static volatile uint32_t	snmp_ref_count = 0;


/* Calling application needs to redirect SNMP logging before prior to this
 * function.
 */

bool
pgm_snmp_init (
	pgm_error_t**	error
	)
{
	if (pgm_atomic_exchange_and_add32 (&snmp_ref_count, 1) > 0)
		return TRUE;

	if (pgm_agentx_subagent)
	{
		pgm_minor (_("Configuring as SNMP AgentX sub-agent."));
		if (pgm_agentx_socket)
		{
			pgm_minor (_("Using AgentX socket %s."), pgm_agentx_socket);
			netsnmp_ds_set_string (NETSNMP_DS_APPLICATION_ID,
						NETSNMP_DS_AGENT_X_SOCKET,
						pgm_agentx_socket);
		}
		netsnmp_ds_set_boolean (NETSNMP_DS_APPLICATION_ID,
					NETSNMP_DS_AGENT_ROLE,
					TRUE);
	}

	pgm_minor (_("Initialising SNMP agent."));
	if (0 != init_agent (pgm_snmp_appname)) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_SNMP,
			     PGM_ERROR_FAILED,
			     _("Initialise SNMP agent: see SNMP log for further details."));
		goto err_cleanup;
	}

	if (!pgm_mib_init (error)) {
		goto err_cleanup;
	}

/* read config and parse mib */
	pgm_minor (_("Initialising SNMP."));
	init_snmp (pgm_snmp_appname);

	if (!pgm_agentx_subagent)
	{
		pgm_minor (_("Connecting to SNMP master agent."));
		if (0 != init_master_agent ()) {
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_SNMP,
				     PGM_ERROR_FAILED,
				     _("Initialise SNMP master agent: see SNMP log for further details."));
			snmp_shutdown (pgm_snmp_appname);
			goto err_cleanup;
		}
	}

/* create notification channel */
	if (0 != pgm_notify_init (&snmp_notify)) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_SNMP,
			     pgm_error_from_errno (errno),
			     _("Creating SNMP notification channel: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		snmp_shutdown (pgm_snmp_appname);
		goto err_cleanup;
	}

/* spawn thread to handle SNMP requests */
#ifndef _WIN32
	const int status = pthread_create (&snmp_thread, NULL, &snmp_routine, NULL);
	if (0 != status) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_SNMP,
			     pgm_error_from_errno (errno),
			     _("Creating SNMP thread: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		snmp_shutdown (pgm_snmp_appname);
		goto err_cleanup;
	}
#else
	snmp_thread = (HANDLE)_beginthreadex (NULL, 0, &snmp_routine, NULL, 0, NULL);
	const int save_errno = errno;
	if (0 == snmp_thread) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_SNMP,
			     pgm_error_from_errno (save_errno),
			     _("Creating SNMP thread: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), save_errno));
		snmp_shutdown (pgm_snmp_appname);
		goto err_cleanup;
	}
#endif /* _WIN32 */
	return TRUE;
err_cleanup:
	if (pgm_notify_is_valid (&snmp_notify)) {
		pgm_notify_destroy (&snmp_notify);
	}
	pgm_atomic_dec32 (&snmp_ref_count);
	return FALSE;
}

/* Terminate SNMP thread and free resources.
 */

bool
pgm_snmp_shutdown (void)
{
	pgm_return_val_if_fail (pgm_atomic_read32 (&snmp_ref_count) > 0, FALSE);

	if (pgm_atomic_exchange_and_add32 (&snmp_ref_count, (uint32_t)-1) != 1)
		return TRUE;

	pgm_notify_send (&snmp_notify);
#ifndef _WIN32
	pthread_join (snmp_thread, NULL);
#else
	WaitForSingleObject (snmp_thread, INFINITE);
	CloseHandle (snmp_thread);
#endif
	pgm_notify_destroy (&snmp_notify);
	snmp_shutdown (pgm_snmp_appname);
	return TRUE;
}

/* Thread routine for processing SNMP requests
 */

static
#ifndef _WIN32
void*
#else
unsigned
__stdcall
#endif
snmp_routine (
	PGM_GNUC_UNUSED	void*	arg
	)
{
	const SOCKET notify_fd = pgm_notify_get_socket (&snmp_notify);

	for (;;)
	{
		int fds = 0, block = 1;
		fd_set fdset;
		struct timeval timeout;

		FD_ZERO(&fdset);
		snmp_select_info (&fds, &fdset, &timeout, &block);
		FD_SET(notify_fd, &fdset);
		if ((notify_fd + 1) > fds)
			fds = notify_fd + 1;
		fds = select (fds, &fdset, NULL, NULL, block ? NULL : &timeout);
		if (FD_ISSET(notify_fd, &fdset))
			break;
		if (fds)
			snmp_read (&fdset);
		else
			snmp_timeout();
	}

/* cleanup */
#ifndef _WIN32
	return NULL;
#else
	_endthread();
	return 0;
#endif /* WIN32 */
}

/* eof */

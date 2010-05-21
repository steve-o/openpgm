/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * basic logging.
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

#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#ifndef G_OS_WIN32
#	include <netdb.h>
#endif
#include <pgm/framework.h>
#include "pgm/log.h"


/* globals */

#define TIME_FORMAT		"%Y-%m-%d %H:%M:%S "

static int log_timezone = 0;
static char log_hostname[NI_MAXHOST + 1];

static void glib_log_handler (const gchar*, GLogLevelFlags, const gchar*, gpointer);
static void pgm_log_handler (const int, const char*, void*);


/* calculate time zone offset in seconds
 */

bool
log_init ( void )
{
/* time zone offset */
	time_t t = time(NULL);
	struct tm sgmt, *gmt = &sgmt;
	*gmt = *gmtime(&t);
	struct tm* loc = localtime(&t);
	log_timezone = (loc->tm_hour - gmt->tm_hour) * 60 * 60 +
		     (loc->tm_min  - gmt->tm_min) * 60;
	int dir = loc->tm_year - gmt->tm_year;
	if (!dir) dir = loc->tm_yday - gmt->tm_yday;
	log_timezone += dir * 24 * 60 * 60;
//	printf ("timezone offset %u seconds.\n", log_timezone);
	gethostname (log_hostname, sizeof(log_hostname));
	g_log_set_handler ("Pgm",		G_LOG_LEVEL_MASK, glib_log_handler, NULL);
	g_log_set_handler ("Pgm-Http",		G_LOG_LEVEL_MASK, glib_log_handler, NULL);
	g_log_set_handler ("Pgm-Snmp",		G_LOG_LEVEL_MASK, glib_log_handler, NULL);
	g_log_set_handler (NULL,		G_LOG_LEVEL_MASK, glib_log_handler, NULL);
	pgm_log_set_handler (pgm_log_handler, NULL);
	return 0;
}

/* log callback
 */
static void
glib_log_handler (
	const gchar*			log_domain,
	G_GNUC_UNUSED GLogLevelFlags	log_level,
	const gchar*			message,
	G_GNUC_UNUSED gpointer		unused_data
	)
{
#ifdef G_OS_UNIX
	struct iovec iov[7];
	struct iovec* v = iov;
	time_t now;
	time (&now);
	const struct tm* time_ptr = localtime(&now);
	char tbuf[1024];
	strftime(tbuf, sizeof(tbuf), TIME_FORMAT, time_ptr);
	v->iov_base = tbuf;
	v->iov_len = strlen(tbuf);
	v++;
	v->iov_base = log_hostname;
	v->iov_len = strlen(log_hostname);
	v++;
	if (log_domain) {
		v->iov_base = " ";
		v->iov_len = 1;
		v++;
		v->iov_base = log_domain;
		v->iov_len = strlen(log_domain);
		v++;
	}
	v->iov_base = ": ";
	v->iov_len = 2;
	v++;
	v->iov_base = message;
	v->iov_len = strlen(message);
	v++;
	v->iov_base = "\n";
	v->iov_len = 1;
	v++;
	writev (STDOUT_FILENO, iov, v - iov);
#else
	time_t now;
	time (&now);
	const struct tm* time_ptr = localtime(&now);
	char s[1024];
	strftime(s, sizeof(s), TIME_FORMAT, time_ptr);
	write (STDOUT_FILENO, s, strlen(s));
	write (STDOUT_FILENO, log_hostname, strlen(log_hostname));
	if (log_domain) {
		write (STDOUT_FILENO, " ", 1);
		write (STDOUT_FILENO, log_domain, strlen(log_domain));
	}
	write (STDOUT_FILENO, ": ", 2);
	write (STDOUT_FILENO, message, strlen(message));
	write (STDOUT_FILENO, "\n", 1);
#endif
}

static void
pgm_log_handler (
	const int		pgm_log_level,
	const char*		message,
	G_GNUC_UNUSED void*	closure
	)
{
	GLogLevelFlags glib_log_level;

	switch (pgm_log_level) {
	case PGM_LOG_LEVEL_DEBUG:	glib_log_level = G_LOG_LEVEL_DEBUG; break;
	case PGM_LOG_LEVEL_TRACE:	glib_log_level = G_LOG_LEVEL_DEBUG; break;
	case PGM_LOG_LEVEL_MINOR:	glib_log_level = G_LOG_LEVEL_INFO; break;
	case PGM_LOG_LEVEL_NORMAL:	glib_log_level = G_LOG_LEVEL_MESSAGE; break;
	case PGM_LOG_LEVEL_WARNING:	glib_log_level = G_LOG_LEVEL_WARNING; break;
	case PGM_LOG_LEVEL_ERROR:	glib_log_level = G_LOG_LEVEL_CRITICAL; break;
	case PGM_LOG_LEVEL_FATAL:	glib_log_level = G_LOG_LEVEL_ERROR; break;
	}

	g_log ("Pgm", glib_log_level, message, NULL);
}

/* eof */

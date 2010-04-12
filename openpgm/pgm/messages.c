/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * basic message reporting.
 *
 * Copyright (c) 2010 Miru Limited.
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/thread.h"


/* globals */

/* bit mask for trace role modules */
int pgm_log_mask = 0xffff;

int pgm_min_log_level = PGM_LOG_LEVEL_DEBUG;


/* locals */

static const char log_levels[8][6] = {
	"Uknown",
	"Debug",
	"Trace",
	"Minor",
	"Info",
	"Warn",
	"Error",
	"Fatal"
};

static pgm_mutex_t log_mutex;
static pgm_log_func_t log_handler = NULL;
static gpointer log_handler_closure = NULL;


static inline
const gchar*
log_level_text (
	const int	log_level
	)
{
	switch (log_level) {
	default:			return log_levels[0];
	case PGM_LOG_LEVEL_DEBUG:	return log_levels[1];
	case PGM_LOG_LEVEL_TRACE:	return log_levels[2];
	case PGM_LOG_LEVEL_MINOR:	return log_levels[3];
	case PGM_LOG_LEVEL_NORMAL:	return log_levels[4];
	case PGM_LOG_LEVEL_WARNING:	return log_levels[5];
	case PGM_LOG_LEVEL_ERROR:	return log_levels[6];
	case PGM_LOG_LEVEL_FATAL:	return log_levels[7];
	}
}

void
pgm_messages_init (void)
{
	pgm_mutex_init (&log_mutex);
}

void
pgm_messages_shutdown (void)
{
	pgm_mutex_free (&log_mutex);
}

/* set application handler for log messages, returns previous value,
 * default handler value is NULL.
 */

pgm_log_func_t
pgm_log_set_handler (
	pgm_log_func_t		handler,
	gpointer		closure
	)
{
	pgm_mutex_lock (&log_mutex);
	log_handler		= handler;
	log_handler_closure	= closure;
	pgm_mutex_unlock (&log_mutex);
}

void
pgm__log (
	const gint		log_level,
	const gchar*		format,
	...
	)
{
	va_list args;

	va_start (args, format);
	pgm__logv (log_level, format, args);
	va_end (args);
}

void
pgm__logv (
	const gint		log_level,
	const gchar*		format,
	va_list			va_args
	)
{
	char tbuf[ 1024 ];
	int offset;

	pgm_mutex_lock (&log_mutex);
	offset = sprintf (tbuf, "%s: ", log_level_text (log_level));
	vsnprintf (tbuf+offset, sizeof(tbuf)-offset, format, va_args);
	tbuf[ sizeof(tbuf) ] = '\0';
	if (log_handler)
		log_handler (log_level, tbuf, log_handler_closure);
	else {
		write (STDOUT_FILENO, tbuf, strlen (tbuf));
		write (STDOUT_FILENO, "\n", 1);
	}
		
	pgm_mutex_unlock (&log_mutex);
}

/* eof */

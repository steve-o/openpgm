/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * basic message reporting.
 *
 * Copyright (c) 2010-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#ifndef _WIN32
#	include <unistd.h>
#else
#	include <io.h>
#endif
#include <impl/framework.h>


/* globals */

/* bit mask for trace role modules */
int pgm_log_mask PGM_GNUC_READ_MOSTLY		= 0xffff;
int pgm_min_log_level PGM_GNUC_READ_MOSTLY	= PGM_LOG_LEVEL_NORMAL;


/* locals */

static const char log_levels[8][7] = {
	"Uknown",
	"Debug",
	"Trace",
	"Minor",
	"Info",
	"Warn",
	"Error",
	"Fatal"
};

static volatile uint32_t	messages_ref_count = 0;
static pgm_mutex_t		messages_mutex;
static pgm_log_func_t 		log_handler PGM_GNUC_READ_MOSTLY = NULL;
static void* 			log_handler_closure PGM_GNUC_READ_MOSTLY = NULL;

static inline const char* log_level_text (const int) PGM_GNUC_PURE;


static inline
const char*
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

/* reference counted init and shutdown
 */

void
pgm_messages_init (void)
{
	char *log_mask, *min_log_level;
	size_t len;
	errno_t err;

	if (pgm_atomic_exchange_and_add32 (&messages_ref_count, 1) > 0)
		return;

	pgm_mutex_init (&messages_mutex);

	err = pgm_dupenv_s (&log_mask, &len, "PGM_LOG_MASK");
	if (!err && len > 0) {
		unsigned int value = 0;
		if (1 == pgm_sscanf_s (log_mask, "0x%4x", &value))
			pgm_log_mask = value;
		pgm_free (log_mask);
	}

	err = pgm_dupenv_s (&min_log_level, &len, "PGM_MIN_LOG_LEVEL");
	if (!err && len > 0) {
		switch (min_log_level[0]) {
		case 'D':	pgm_min_log_level = PGM_LOG_LEVEL_DEBUG; break;
		case 'T':	pgm_min_log_level = PGM_LOG_LEVEL_TRACE; break;
		case 'M':	pgm_min_log_level = PGM_LOG_LEVEL_MINOR; break;
		case 'N':	pgm_min_log_level = PGM_LOG_LEVEL_NORMAL; break;
		case 'W':	pgm_min_log_level = PGM_LOG_LEVEL_WARNING; break;
		case 'E':	pgm_min_log_level = PGM_LOG_LEVEL_ERROR; break;
		case 'F':	pgm_min_log_level = PGM_LOG_LEVEL_FATAL; break;
		default: break;
		}
		pgm_free (min_log_level);
	}
}

void
pgm_messages_shutdown (void)
{
	pgm_return_if_fail (pgm_atomic_read32 (&messages_ref_count) > 0);

	if (pgm_atomic_exchange_and_add32 (&messages_ref_count, (uint32_t)-1) != 1)
		return;

	pgm_mutex_free (&messages_mutex);
}

/* set application handler for log messages, returns previous value,
 * default handler value is NULL.
 */

pgm_log_func_t
pgm_log_set_handler (
	pgm_log_func_t		handler,
	void*			closure
	)
{
	pgm_log_func_t previous_handler;
	const uint32_t count = pgm_atomic_read32 (&messages_ref_count);

/* cannot use mutexes for initialising log handler before pgm_init() for
 * locking systems that do not accept static initialization, e.g. Windows
 * critical sections.
 */
	if (count > 0) pgm_mutex_lock (&messages_mutex);
	previous_handler	= log_handler;
	log_handler		= handler;
	log_handler_closure	= closure;
	if (count > 0) pgm_mutex_unlock (&messages_mutex);
	return previous_handler;
}

PGM_GNUC_INTERNAL
void
pgm__log (
	const int		log_level,
	const char*		format,
	...
	)
{
	va_list args;

	va_start (args, format);
	pgm__logv (log_level, format, args);
	va_end (args);
}

PGM_GNUC_INTERNAL
void
pgm__logv (
	const int		log_level,
	const char*		format,
	va_list			args
	)
{
	char tbuf[1024];

	pgm_mutex_lock (&messages_mutex);
	const int offset = pgm_snprintf_s (tbuf, sizeof (tbuf), _TRUNCATE, "%s: ", log_level_text (log_level));
	pgm_vsnprintf_s (tbuf + offset, sizeof(tbuf) - offset, _TRUNCATE, format, args);
	if (log_handler)
		log_handler (log_level, tbuf, log_handler_closure);
	else {
/* ignore return value */
		(void) write (STDOUT_FILENO, tbuf, strlen (tbuf));
		(void) write (STDOUT_FILENO, "\n", 1);
	}
		
	pgm_mutex_unlock (&messages_mutex);
}

/* eof */

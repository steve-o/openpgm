/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Re-entrant safe signal handling.
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

#define _GNU_SOURCE
#include <signal.h>		/* strsignal() */
#include <pgm/framework.h>
#include "pgm/signal.h"


//#define SIGNAL_DEBUG


/* globals */

static pgm_sighandler_t		signal_list[NSIG];
static int			signal_pipe[2];
static GIOChannel*		signal_io = NULL;

static void on_signal (int);
static gboolean	on_io_signal (GIOChannel*, GIOCondition, gpointer);
static const char* cond_string (GIOCondition);


/* install signal handler and return unix fd to add to event loop
 */

gboolean
pgm_signal_install (
	int			signum,
	pgm_sighandler_t	handler,
	gpointer		user_data
	)
{
	pgm_debug ("pgm_signal_install (signum:%d handler:%p user_data:%p)",
		signum, (gpointer)handler, user_data);

	if (NULL == signal_io)
	{
#ifdef G_OS_UNIX
		if (pipe (signal_pipe))
#else
		if (_pipe (signal_pipe, 4096, _O_BINARY | _O_NOINHERIT))
#endif
			return FALSE;

		pgm_sockaddr_nonblocking (signal_pipe[0], TRUE);
		pgm_sockaddr_nonblocking (signal_pipe[1], TRUE);
/* add to evm */
		signal_io = g_io_channel_unix_new (signal_pipe[0]);
		g_io_add_watch (signal_io, G_IO_IN, on_io_signal, user_data);
	}

	signal_list[signum] = handler;
	return (SIG_ERR != signal (signum, on_signal));
}

/* process signal from operating system
 */

static
void
on_signal (
	int		signum
	)
{
	pgm_debug ("on_signal (signum:%d)", signum);
	if (write (signal_pipe[1], &signum, sizeof(signum)) != sizeof(signum))
	{
#ifndef _WIN32
		pgm_warn ("Unix signal %s (%d) lost", strsignal (signum), signum);
#else
		pgm_warn ("Unix signal (%d) lost", signum);
#endif
	}
}

/* process signal from pipe
 */

static
gboolean
on_io_signal (
	GIOChannel*	source,
	GIOCondition	cond,
	gpointer	user_data
	)
{
/* pre-conditions */
	g_assert (NULL != source);
	g_assert (G_IO_IN == cond);

	pgm_debug ("on_io_signal (source:%p cond:%s user_data:%p)",
		(gpointer)source, cond_string (cond), user_data);

	int signum;
	const gsize bytes_read = read (g_io_channel_unix_get_fd (source), &signum, sizeof(signum));

	if (sizeof(signum) == bytes_read)
	{
		signal_list[signum] (signum, user_data);
	}
	else
	{
		pgm_warn ("Lost data in signal pipe, read %" G_GSIZE_FORMAT " byte%s expected %" G_GSIZE_FORMAT ".",
				bytes_read, bytes_read > 1 ? "s" : "", sizeof(signum));
	}

	return TRUE;
}

static
const char*
cond_string (
	GIOCondition	cond
	)
{
	const char* c;

	switch (cond) {
	case G_IO_IN:		c = "G_IO_IN"; break;
	case G_IO_OUT:		c = "G_IO_OUT"; break;
	case G_IO_PRI:		c = "G_IO_PRI"; break;
	case G_IO_ERR:		c = "G_IO_ERR"; break;
	case G_IO_HUP:		c = "G_IO_HUP"; break;
	case G_IO_NVAL:		c = "G_IO_NVAL"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}


/* eof */

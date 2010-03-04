/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable fail fast memory allocation.
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

#include <stdlib.h>

#include <glib.h>

#include "pgm/malloc.h"

//#define ALLOC_DEBUG

#ifndef ALLOC_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


gpointer
pgm_malloc (
	gulong		n_bytes
	)
{
	if (G_LIKELY (n_bytes))
	{
		gpointer mem = malloc (n_bytes);
		if (mem)
			return mem;

		g_error ("%s: failed to allocate %lu bytes", G_STRLOC, n_bytes);
	}
	return NULL;
}

gpointer
pgm_malloc0 (
	gulong		n_bytes
	)
{
	if (G_LIKELY (n_bytes))
	{
		gpointer mem = calloc (1, n_bytes);
		if (mem)
			return mem;

		g_error ("%s: failed to allocate %lu bytes", G_STRLOC, n_bytes);
	}
	return NULL;
}

gpointer
pgm_memdup (
	gconstpointer	mem,
	guint		n_bytes
	)
{
	gpointer new_mem;

	if (G_LIKELY (mem))
	{
		new_mem = pgm_malloc (n_bytes);
		memcpy (new_mem, mem, n_bytes);
	}
	else
		new_mem = NULL;

	return new_mem;
}

void
pgm_free (
	gpointer	mem
	)
{
	if (G_LIKELY (mem))
		free (mem);
}

/* eof */

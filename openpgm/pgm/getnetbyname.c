/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable implementation of getnetbyname
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

#include <stdio.h>
#include <impl/framework.h>


//#define GETNETBYNAME_DEBUG

/* locals */

#ifndef _WIN32
static char netdb[] = "/etc/networks";
#else
/* NB: 32-bit applications may read %systemroot%\SysWOW64\drivers\etc */
static char netdb[] = "%systemroot%\\system32\\drivers\\etc\\networks";
#endif
static FILE* netfh = NULL;

static void pgm_setnetent (void);
static struct pgm_netent_t *pgm_getnetent (void);
static void pgm_endnetent (void);


static
void
pgm_setnetent (void)
{
	if (NULL == netfh)
		netfh = fopen (netdb, "r");
	else
		rewind (netfh);
}

static
void
pgm_endnetent (void)
{
	if (NULL != netfh) {
		fclose (netfh);
		netfh = NULL;
	}
}

/* link-local 169.254.0.0 alias1 alias2
 */

static
struct pgm_netent_t*
pgm_getnetent (void)
{
	return NULL;
}

/* Lookup network by name in the /etc/networks database.
 *
 * returns 0 on success, returns -1 on invalid address.
 */

struct pgm_netent_t*
pgm_getnetbyname (
	const char*	name
	)
{
	struct pgm_netent_t *p;
	char **cp;

	pgm_setnetent ();
	while (p = pgm_getnetent()) {
		if (!strncmp (p->n_name, name, 1024))
			break;
		for (cp = p->n_aliases; *cp != 0; cp++)
			if (!strncmp (*cp, name, 1024))
				goto found;
	}
found:
	pgm_endnetent();
	return p;
}

/* eof */


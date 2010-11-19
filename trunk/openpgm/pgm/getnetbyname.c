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

#define MAXALIASES	35

#ifndef _WIN32
static char netdb[] = "/etc/networks";
#else
/* NB: 32-bit applications may read %systemroot%\SysWOW64\drivers\etc */
static char netdb[] = "%systemroot%\\system32\\drivers\\etc\\networks";
#endif
static FILE* netfh = NULL;
static char line[BUFSIZ+1];
static char *net_aliases[MAXALIASES];
static struct pgm_netent_t net;

static void _pgm_compat_setnetent (void);
static struct pgm_netent_t *_pgm_compat_getnetent (void);
static void _pgm_compat_endnetent (void);
static struct pgm_netent_t* _pgm_compat_getnetbyname (const char*);


static
void
_pgm_compat_setnetent (void)
{
	if (NULL == netfh) {
#ifdef _WIN32
		char expanded[MAX_PATH];
		if (0 == ExpandEnvironmentStrings (netdb, expanded, sizeof (expanded))) {
			const DWORD save_errno = GetLastError();
			char winstr[1024];
			pgm_warn (_("Cannot expand netdb path \"%s\": %s"),
				  netdb,
				  pgm_win_strerror (winstr, sizeof (winstr), save_errno));
			return;
		}
#endif
#ifndef _WIN32
		netfh = fopen (netdb, "r");
#else
		netfh = fopen (expanded, "r");
#endif
		if (NULL == netfh) {
			char errbuf[1024];
			pgm_warn (_("Opening netdb file \"%s\" failed: %s"),
				  netdb,
				  pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		}
	} else {
		rewind (netfh);
	}
}

static
void
_pgm_compat_endnetent (void)
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
_pgm_compat_getnetent (void)
{
	char *p, *cp, **q;

	if (NULL == netfh) {
		_pgm_compat_setnetent();
		if (NULL == netfh)
			return NULL;
	}

again:
	if (NULL == (p = fgets (line, BUFSIZ, netfh)))
		return NULL;
	if ('#' == *p)		/* comment */
		goto again;
	cp = strpbrk (p, "#\n");
 	if (NULL == cp)
		goto again;
	*cp = '\0';
	net.n_name = p;
	cp = strpbrk (p, " \t");
 	if (NULL == cp)
		goto again;
	*cp++ = '\0';
	while (' ' == *cp || '\t' == *cp)
		cp++;
	p = strpbrk (cp, " \t");
	if (NULL != p)
		*p++ = '\0';
	if (0 == pgm_inet_network (cp, (struct in_addr*)net.n_net)) {
		net.n_addrtype = AF_INET;
		net.n_length = sizeof (struct in_addr);
	} else if (0 == pgm_inet6_network (cp, (struct in6_addr*)net.n_net)) {
		net.n_addrtype = AF_INET6;
		net.n_length = sizeof (struct in6_addr);
	} else {
/* cannot resolve address, fail instead of returning junk address */
		return NULL;
	}
	q = net.n_aliases = net_aliases;
	if (NULL != p) {
/* some versions stick the address as the first alias, some default to NULL */
		cp = p;
		while (cp && *cp) {
			if (' ' == *cp || '\t' == *cp) {
				cp++;
				continue;
			}
			if (q < &net_aliases[MAXALIASES - 1])
				*q++ = cp;
			cp = strpbrk (cp, " \t");
			if (NULL != cp)
				*cp++ = '\0';
		}
	}
	*q = NULL;
	return &net;
}

/* Lookup network by name in the /etc/networks database.
 *
 * returns 0 on success, returns -1 on invalid address.
 */

static
struct pgm_netent_t*
_pgm_compat_getnetbyname (
	const char*	name
	)
{
	struct pgm_netent_t *p;
	char **cp;

	if (NULL == name)
		return NULL;

	_pgm_compat_setnetent ();
	while (NULL != (p = _pgm_compat_getnetent())) {
		if (!strncmp (p->n_name, name, BUFSIZ))
			break;
		for (cp = p->n_aliases; *cp != 0; cp++)
			if (!strncmp (*cp, name, BUFSIZ))
				goto found;
	}
found:
	_pgm_compat_endnetent();
	return p;
}

#ifdef CONFIG_HAVE_GETNETENT
static
struct pgm_netent_t*
_pgm_native_getnetbyname (
	const char*	name
	)
{
	struct netent *ne;
	char **cp;

	if (NULL == name)
		return NULL;

	setnetent (0);
	while (NULL != (ne = getnetent())) {
		if (!strncmp (ne->n_name, name, BUFSIZ))
			break;
		for (cp = ne->n_aliases; *cp != 0; cp++)
			if (!strncmp (*cp, name, BUFSIZ))
				goto found;
	}
	endnetent();
	return NULL;
found:
	net.n_name     = ne->n_name;
	net.n_aliases  = ne->n_aliases;
	net.n_addrtype = ne->n_addrtype;
	net.n_length   = sizeof (ne->n_net);
	memcpy (net.n_net, &ne->n_net, net.n_length);
	endnetent();
	return &net;
}
#endif

struct pgm_netent_t*
pgm_getnetbyname (
	const char*	name
	)
{
#ifdef CONFIG_HAVE_GETNETENT
	return _pgm_native_getnetbyname (name);
#else
	return _pgm_compat_getnetbyname (name);
#endif
}

/* eof */


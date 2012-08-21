/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable implementation of getnetbyname, returns a network address
 * from a given name.  On Unix systems the mapping is managed by a
 * system service and may be sourced from a network service, cached 
 * locally, or read directly from a local file.  On systems without
 * such a service or when IPv6 is required a local file is supported
 * with compatible parsing of the network-name mapping format.
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
#include <stdio.h>
#include <impl/framework.h>


//#define GETNETBYNAME_DEBUG

/* locals */

#define MAXALIASES	35

#ifndef _WIN32
#	define PATH_NETWORKS	"/etc/networks"
#else
/* NB: 32-bit applications may read %systemroot%\SysWOW64\drivers\etc */
#	define PATH_NETWORKS	"%systemroot%\\system32\\drivers\\etc\\networks"
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
		char*	netdb;
		size_t	envlen;
		errno_t err;

/* permit path override */
		err = pgm_dupenv_s (&netdb, &envlen, "PGM_NETDB");
		if (0 != err || 0 == envlen) {
			netdb = pgm_strdup (PATH_NETWORKS);
		}
#ifdef _WIN32
	{
		char expanded[MAX_PATH];
		if (0 == ExpandEnvironmentStrings ((LPCTSTR)netdb, (LPTSTR)expanded, sizeof (expanded))) {
			const DWORD save_errno = GetLastError();
			char winstr[1024];
			pgm_warn (_("Cannot expand netdb path \"%s\": %s"),
				  netdb,
				  pgm_win_strerror (winstr, sizeof (winstr), save_errno));
/* fall through on original string */
		} else {
			free (netdb);
			netdb = pgm_strdup (expanded);
		}
	}
#endif
		err = pgm_fopen_s (&netfh, netdb, "r");
		if (0 != err) {
			char errbuf[1024];
			pgm_warn (_("Opening netdb file \"%s\" failed: %s"),
				  netdb,
				  pgm_strerror_s (errbuf, sizeof (errbuf), err));
		}

		free (netdb);

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
 *
 * returns address in host-byte order
 */

static
struct pgm_netent_t*
_pgm_compat_getnetent (void)
{
	struct in_addr sin;
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
/* returns address in network order */
	if (0 == pgm_inet_network (cp, &sin)) {
		struct sockaddr_in sa;
		memset (&sa, 0, sizeof (sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = ntohl (sin.s_addr);
		memcpy (&net.n_net, &sa, sizeof (sa));
	} else if (0 != pgm_sa6_network (cp, (struct sockaddr_in6*)&net.n_net)) {
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

#ifdef HAVE_GETNETENT
static
struct pgm_netent_t*
_pgm_native_getnetbyname (
	const char*	name
	)
{
	struct sockaddr_in sa;
	struct in_addr addr;
	struct netent *ne;
	char **cp, **q, **r;
	size_t len;

	if (NULL == name)
		return NULL;

	setnetent (0);
	while (NULL != (ne = getnetent())) {
		if (!strncmp (ne->n_name, name, BUFSIZ))
			goto found;
		for (cp = ne->n_aliases; *cp != 0; cp++)
			if (!strncmp (*cp, name, BUFSIZ))
				goto found;
	}
	endnetent();
	return NULL;
found:
/* copy result into own global static buffer */
	len = strlen (ne->n_name) + 1;
	if (len > BUFSIZ)
		return NULL;
	net.n_name = memcpy (line, ne->n_name, len);
	q = net.n_aliases = net_aliases;
	r = ne->n_aliases;
	while (*r) {
		const size_t alias_len = strlen (*r) + 1;
		if ((len + alias_len) > BUFSIZ)
			break;
		*q++ = memcpy (line + len, *r++, alias_len);
		len += alias_len;
	}
	*q = NULL;
	if (AF_INET != ne->n_addrtype)
		return NULL;
	memset (&sa, 0, sizeof (sa));
	sa.sin_family = ne->n_addrtype;
	addr = pgm_inet_makeaddr (ne->n_net, 0);
	sa.sin_addr.s_addr = ntohl (addr.s_addr);
	memcpy (&net.n_net, &sa, sizeof (sa));
	endnetent();
	return &net;
}
#endif

/* returns addresses in CIDR format in host-byte order, native implementations
 * use pre-CIDR aligned format.
 *
 * ::getnetbyname(loopback)   = 0.0.0.127
 * pgm_getnetbyname(loopback) = 127.0.0.0
 *
 * returns first matching entry, whilst networks file could potentially contain
 * multiple matching entries the native APIs do not support such functionality.
 *
 * cf. networks vs. hosts which getaddrinfo may return a list of results.
 */

struct pgm_netent_t*
pgm_getnetbyname (
	const char*	name
	)
{
#ifdef HAVE_GETNETENT
	char*   netdb;
	size_t  envlen;
	errno_t err;

	err = pgm_dupenv_s (&netdb, &envlen, "PGM_NETDB");
	free (netdb);
	if (0 != err || 0 == envlen) {
/* default use native implementation */
		return _pgm_native_getnetbyname (name);
	}
#endif
	return _pgm_compat_getnetbyname (name);
}

/* eof */


/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable implementation of getprotobyname.  Returns the IP protocol
 * number for the provided protocol name.
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


//#define GETPROTOBYNAME_DEBUG

/* locals */

#define MAXALIASES	35

static char line[BUFSIZ+1];
static char *proto_aliases[MAXALIASES];
static struct pgm_protoent_t proto;

/* re-entrant system APIs are preferred, unfortunately two different decls
 * are often available.
 */

static
struct pgm_protoent_t*
_pgm_native_getprotobyname (
	const char*	name
	)
{
	struct protoent* pe;
	char **q, **r;
	size_t len;

	if (NULL == name)
		return NULL;

#if defined( HAVE_GETPROTOBYNAME_R ) && defined( GETPROTOBYNAME_R_STRUCT_PROTOENT_P )
	char buf[BUFSIZ];
	struct protoent protobuf;
	if (NULL == (pe = getprotobyname_r (name, &protobuf, buf, BUFSIZ)))
		return NULL;
#elif defined( HAVE_GETPROTOBYNAME_R )
	char buf[BUFSIZ];
	struct protoent protobuf;
	if (0 != getprotobyname_r (name, &protobuf, buf, BUFSIZ, &pe) || NULL == pe)
		return NULL;
#else
	if (NULL == (pe = getprotobyname (name)))
		return NULL;
#endif /* HAVE_GETPROTOBYNAME_R */
	len = strlen (pe->p_name) + 1;
	if (len > BUFSIZ)
		return NULL;
	proto.p_name = memcpy (line, pe->p_name, len);
	q = proto.p_aliases = proto_aliases;
	r = pe->p_aliases;
	while (*r) {
		const size_t alias_len = strlen (*r) + 1;
		if ((len + alias_len) > BUFSIZ)
			break;
		*q++ = memcpy (line + len, *r++, alias_len);
		len += alias_len;
	}
	*q = NULL;
	proto.p_proto = pe->p_proto;
	return &proto;
}

struct pgm_protoent_t*
pgm_getprotobyname (
	const char*	name
	)
{
	return _pgm_native_getprotobyname (name);
}

/* eof */


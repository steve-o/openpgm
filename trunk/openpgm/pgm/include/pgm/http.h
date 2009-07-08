/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * HTTP administrative interface
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

#ifndef __PGM_HTTP_H__
#define __PGM_HTTP_H__

#include <glib.h>


G_BEGIN_DECLS

#define PGM_HTTP_DEFAULT_SERVER_PORT	4968

int pgm_http_init (guint16) G_GNUC_WARN_UNUSED_RESULT;
int pgm_http_shutdown (void) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __PGM_SIGNAL_H__ */

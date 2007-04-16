/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic receive window.
 *
 * Copyright (c) 2006 Miru Limited.
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

#ifndef _RXW_H
#define _RXW_H

typedef int (*rxw_callback)(gpointer, guint, gpointer);

#ifdef __cplusplus
extern "C" {
#endif

gpointer rxw_init (guint, guint32, guint32, guint, guint, rxw_callback, gpointer);
int rxw_shutdown (gpointer);

gpointer rxw_alloc (gpointer);
int rxw_push (gpointer, gpointer, guint, guint32, guint32);

int rxw_update (gpointer, guint32, guint32);

#ifdef __cplusplus
}
#endif

#endif /* _RXW_H */

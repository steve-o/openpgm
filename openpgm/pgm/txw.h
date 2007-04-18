/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic transmit window.
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

#ifndef __PGM_TXW_H__
#define __PGM_TXW_H__

G_BEGIN_DECLS


gpointer txw_init (guint, guint32, guint32, guint, guint);
int txw_shutdown (gpointer);

guint32 txw_next_lead (gpointer);
guint32 txw_lead (gpointer);
guint32 txw_trail (gpointer);

int txw_in_window (gpointer, guint32);

gpointer txw_alloc (gpointer);
int txw_push (gpointer, gpointer, guint);
int txw_push_copy (gpointer, gpointer, guint);
int txw_get (gpointer, guint32, gpointer*, guint*);
int txw_pop (gpointer);


G_END_DECLS

#endif /* __PGM_TXW_H__ */

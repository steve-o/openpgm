/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * OpenPGM version.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#include <glib.h>


/* globals */

const guint pgm_major_version = PGM_MAJOR_VERSION;
const guint pgm_minor_version = PGM_MINOR_VERSION;
const guint pgm_micro_version = PGM_MICRO_VERSION;


const gchar*
pgm_check_version (
	guint	required_major,
	guint	required_minor,
	guint	required_micro
	)
{
	gint pgm_effective_micro = 100 * PGM_MINOR_VERSION + PGM_MICRO_VERSION;
	gint required_effective_micro = 100 * required_minor + required_micro;

	if (required_major > PGM_MAJOR_VERSION)
		return "OpenPGM version too old (major mismatch)";
	if (required_major < PGM_MAJOR_VERSION)
		return "OpenPGM version too new (major mismatch)";
	if (required_effective_micro > pgm_effective_micro)
		return "OpenPGM version too old (micro mismatch)";
	return NULL;
}


/* eof */

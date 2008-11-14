/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Dump version details.
 *
 * Copyright (c) 2008 Miru Limited.
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

#include "pgm/pgm.h"


int
main (void)
{
	printf ("BUILD_DATE=%s\n"
		"BUILD_TIME=%s\n"
		"BUILD_PLATFORM=%s\n"
		"BUILD_REVISION=%s\n"
		"PGM_MAJOR_VERSION=%i\n"
		"PGM_MINOR_VERSION=%i\n"
		"PGM_MICRO_VERSION=%i\n",
		pgm_build_date, pgm_build_time, pgm_build_platform, pgm_build_revision,
		pgm_major_version, pgm_minor_version, pgm_micro_version);
	return 0;
}

/* eof */

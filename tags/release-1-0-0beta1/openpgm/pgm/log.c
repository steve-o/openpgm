/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * basic logging.
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


#include <stdio.h>
#include <time.h>

#include "pgm/log.h"


/* globals */

int g_timezone = 0;


/* calculate time zone offset in seconds
 */

gboolean
log_init ( void )
{
/* time zone offset */
	time_t t = time(NULL);
	struct tm sgmt, *gmt = &sgmt;
	*gmt = *gmtime(&t);
	struct tm* loc = localtime(&t);
	g_timezone = (loc->tm_hour - gmt->tm_hour) * 60 * 60 +
		     (loc->tm_min  - gmt->tm_min) * 60;
	int dir = loc->tm_year - gmt->tm_year;
	if (!dir) dir = loc->tm_yday - gmt->tm_yday;
	g_timezone += dir * 24 * 60 * 60;

//	printf ("timezone offset %u seconds.\n", g_timezone);
	return 0;
}

/* format a timestamp with usecs
 */

char*
ts_format (
	int sec,
	int usec
	)
{
	static char buf[sizeof("00:00:00.000000")];
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06u",
		sec / 3600, (sec % 3600) / 60, sec % 60, usec);

	return buf;
}

/* eof */

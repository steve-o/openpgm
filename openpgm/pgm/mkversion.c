/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * OpenPGM version generator.
 *
 * Copyright (c) 2016 Miru Limited.
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
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

int
main(int argc, char* argv[])
{
	char build_date[1024], build_time[1024];
	char build_system[] =
#if defined(__APPLE__)
		"Darwin"
#elif defined(__CYGWIN__)
		"Cygwin"
#elif defined(__MINGW32__)
		"MinGW"
#elif defined(_WIN32)
		"Windows"
#elif defined(_AIX)
		"AIX"
#elif defined(__linux__)
		"Linux"
#elif defined(__FreeBSD__)
		"FreeBSD"
#elif defined(__NetBSD__)
		"NetBSD"
#elif defined(__OpenBSD__)
		"OpenBSD"
#elif defined(__sun)
		"Solaris"
#endif
		"";
	char build_machine[] =
#if defined(__i386__) || defined(__i386)
		"i386"
#elif defined(__x86_64__) || defined(__amd64) || defined(_WIN64)
		"x86-64"
#elif defined(__sparc__) || defined(__sparc) || defined(__sparcv9)
		"Sparc"
#endif
		"";
	time_t t = time(NULL);
	struct tm* tmp = localtime(&t);
	strftime(build_date, sizeof(build_date), "%Y-%m-%d", tmp);
	strftime(build_time, sizeof(build_time), "%H:%M:%S", tmp);
	puts("#ifdef HAVE_CONFIG_H");
	puts("#       include <config.h>");
	puts("#endif");
	puts("#include <impl/framework.h>");
	puts("#include <pgm/version.h>");
	printf("const unsigned pgm_major_version = 5;\n");
	printf("const unsigned pgm_minor_version = 2;\n");
	printf("const unsigned pgm_micro_version = 127;\n");
	printf("const char* pgm_build_date = \"%s\";\n", build_date);
	printf("const char* pgm_build_time = \"%s\";\n", build_time);
	printf("const char* pgm_build_system = \"%s\";\n", build_system);
	printf("const char* pgm_build_machine = \"%s\";\n", build_machine);
	printf("const char* pgm_build_revision = \"\";\n");

	return 0;
}

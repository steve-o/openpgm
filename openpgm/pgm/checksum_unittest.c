/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM checksum routines
 *
 * Copyright (c) 2009-2010 Miru Limited.
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


#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/param.h>
#include <glib.h>
#include <check.h>

#if defined( BYTE_ORDER )
#	define PGM_BYTE_ORDER		BYTE_ORDER
#	define PGM_BIG_ENDIAN		BIG_ENDIAN
#	define PGM_LITTLE_ENDIAN	LITTLE_ENDIAN
#elif defined( __BYTE_ORDER )
#	define PGM_BYTE_ORDER		__BYTE_ORDER
#	define PGM_BIG_ENDIAN		__BIG_ENDIAN
#	define PGM_LITTLE_ENDIAN	__LITTLE_ENDIAN
#elif defined( __sun )
#	define PGM_LITTLE_ENDIAN	1234
#	define PGM_BIG_ENDIAN		4321
#	if defined( _BIT_FIELDS_LTOH )
#		define PGM_BYTE_ORDER		PGM_LITTLE_ENDIAN
#	elif defined( _BIT_FIELDS_HTOL )
#		define PGM_BYTE_ORDER		PGM_BIG_ENDIAN
#	else
#		error "Unknown bit field order for Sun Solaris."
#	endif
#else
#	error "BYTE_ORDER not supported."
#endif

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */


/* mock functions for external references */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

#define CHECKSUM_DEBUG
#include "checksum.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	uint16_t
 *	pgm_inet_checksum (
 *		const void*		src,
 *		uint16_t		len,
 *		uint16_t		csum
 *	)
 */

START_TEST (test_inet_pass_001)
{
	const char source[]  = "i am not a string";
	const guint16 answer = 0x1fda;		/* network order */

	guint16 csum = pgm_inet_checksum (source, sizeof(source), 0);
/* function calculates answer in host order */
	csum = g_htons (csum);
	g_message ("IP checksum of \"%s\" (%u) is %u (%u)",
		source, (unsigned)sizeof(source), csum, answer);

	fail_unless (answer == csum, "checksum mismatch");
}
END_TEST

START_TEST (test_inet_pass_002)
{
	char* source = alloca (65535);
	for (unsigned i = 0, j = 0; i < 65535; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = 0x3fc0;		/* network order */

	guint16 csum = pgm_inet_checksum (source, 65535, 0);
/* function calculates answer in host order */
	csum = g_htons (csum);
	g_message ("IP checksum of 64KB is %u (%u)",
		csum, answer);

	fail_unless (answer == csum, "checksum mismatch");
}
END_TEST

START_TEST (test_inet_fail_001)
{
	pgm_inet_checksum (NULL, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	guint16
 *	pgm_csum_fold (
 *		guint32			csum
 *	)
 */

START_TEST (test_fold_pass_001)
{
	const guint32 csum   = 0xdd250300;	/* network order */
	const guint16 answer = 0x1fda;

	guint16 folded_csum = pgm_csum_fold (g_ntohl (csum));
	folded_csum = g_htons (folded_csum);
	g_message ("32-bit checksum %u folds into %u (%u)", csum, folded_csum, answer);

	fail_unless (answer == folded_csum, "folded checksum mismatch");
}
END_TEST


/* target:
 *	guint32
 *	pgm_csum_partial (
 *		const void*		src,
 *		guint			len,
 *		guint32			sum
 *	)
 */

START_TEST (test_partial_pass_001)
{
	const char source[]  = "i am not a string";
#if PGM_BYTE_ORDER == PGM_BIG_ENDIAN
	const guint32 answer = 0x0000e025;	/* network order */
#elif PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	const guint32 answer = 0xe0250000;	/* network order */
#else
#	error "PGM_BYTE_ORDER not supported."
#endif

	guint32 csum = pgm_csum_partial (source, sizeof(source), 0);
	csum = g_htonl (csum);
	g_message ("Partial checksum of \"%s\" is %u (%u)", source, csum, answer);

	fail_unless (answer == csum, "checksum mismatch");
}
END_TEST

START_TEST (test_partial_fail_001)
{
	pgm_csum_partial (NULL, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	guint32
 *	pgm_csum_partial_copy (
 *		const void*		src,
 *		void*			dst,
 *		guint			len,
 *		guint32			sum
 *	)
 */

START_TEST (test_partial_copy_pass_001)
{
	const char source[] = "i am not a string";
#if PGM_BYTE_ORDER == PGM_BIG_ENDIAN
	const guint32 answer = 0x0000e025;	/* network order */
#elif PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	const guint32 answer = 0xe0250000;	/* network order */
#else
#	error "BYTE_ORDER not supported."
#endif

	char dest[1024];
	guint32 csum_source = pgm_csum_partial_copy (source, dest, sizeof(source), 0);
	csum_source = g_htonl (csum_source);
	guint32 csum_dest   = pgm_csum_partial (dest, sizeof(source), 0);
	csum_dest = g_htonl (csum_dest);
	g_message ("Partial copy checksum of \"%s\" is %u, partial checksum is %u (%u)",
		   source, csum_source, csum_dest, answer);
	fail_unless (answer == csum_source, "checksum mismatch in partial-copy");
	fail_unless (answer == csum_dest,   "checksum mismatch in partial");
}
END_TEST

START_TEST (test_partial_copy_fail_001)
{
	pgm_csum_partial_copy (NULL, NULL, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	guint32
 *	pgm_csum_block_add (
 *		guint32			csum,
 *		guint32			csum2,
 *		guint			offset
 *	)
 */

START_TEST (test_block_add_pass_001)
{
	const char source[] = "i am not a string";
	const guint16 answer = 0x1fda;		/* network order */

	guint32 csum_a = pgm_csum_partial (source, sizeof(source) / 2, 0);
	guint32 csum_b = pgm_csum_partial (source + (sizeof(source) / 2), sizeof(source) - (sizeof(source) / 2), 0);
	guint32 csum   = pgm_csum_block_add (csum_a, csum_b, sizeof(source) / 2);
	guint16 fold   = pgm_csum_fold (csum);
/* convert to display in network order */
	csum_a = g_htonl (csum_a);
	csum_b = g_htonl (csum_b);
	csum   = g_htonl (csum);
	fold   = g_htons (fold);
	g_message ("Checksum A:%u + B:%u = %u -> %u (%u)",
		   csum_a, csum_b, csum, fold, answer);
	fail_unless (answer == fold, "checksum mismatch");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_inet = tcase_create ("inet");
	suite_add_tcase (s, tc_inet);
	tcase_add_test (tc_inet, test_inet_pass_001);
	tcase_add_test (tc_inet, test_inet_pass_002);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_inet, test_inet_fail_001, SIGABRT);
#endif

	TCase* tc_fold = tcase_create ("fold");
	suite_add_tcase (s, tc_fold);
	tcase_add_test (tc_fold, test_fold_pass_001);

	TCase* tc_block_add = tcase_create ("block-add");
	suite_add_tcase (s, tc_block_add);
	tcase_add_test (tc_block_add, test_block_add_pass_001);

	TCase* tc_partial = tcase_create ("partial");
	suite_add_tcase (s, tc_partial);
	tcase_add_test (tc_partial, test_partial_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_partial, test_partial_fail_001, SIGABRT);
#endif

	TCase* tc_partial_copy = tcase_create ("partial-copy");
	suite_add_tcase (s, tc_partial_copy);
	tcase_add_test (tc_partial_copy, test_partial_copy_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_partial_copy, test_partial_copy_fail_001, SIGABRT);
#endif
	return s;
}

static
Suite*
make_master_suite (void)
{
	Suite* s = suite_create ("Master");
	return s;
}

int
main (void)
{
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */

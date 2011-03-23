/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for rate regulation.
 *
 * Copyright (c) 2009 Miru Limited.
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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */


#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now

#define RATE_CONTROL_DEBUG
#include "rate_control.c"

static pgm_time_t mock_pgm_time_now = 0x1;
static pgm_time_t _mock_pgm_time_update_now (void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;


/* mock functions for external references */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

static
pgm_time_t
_mock_pgm_time_update_now (void)
{
	g_debug ("mock_pgm_time_now: %" PGM_TIME_FORMAT, mock_pgm_time_now);
	return mock_pgm_time_now;
}


/* target:
 *	void
 *	pgm_rate_create (
 *		pgm_rate_t*		bucket_,
 *		const ssize_t		rate_per_sec,
 *		const size_t		iphdr_len,
 *		const uint16_t		max_tpdu
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_rate_t rate;
	memset (&rate, 0, sizeof(rate));
	pgm_rate_create (&rate, 100*1000, 10, 1500);
}
END_TEST

START_TEST (test_create_fail_001)
{
	pgm_rate_create (NULL, 0, 0, 1500);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rate_destroy (
 *		pgm_rate_t*		bucket
 *	)
 */

START_TEST (test_destroy_pass_001)
{
	pgm_rate_t rate;
	memset (&rate, 0, sizeof(rate));
	pgm_rate_create (&rate, 100*1000, 10, 1500);
	pgm_rate_destroy (&rate);
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	pgm_rate_destroy (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	bool
 *	pgm_rate_check (
 *		pgm_rate_t*		bucket,
 *		const size_t		data_size,
 *		const bool		is_nonblocking
 *	)
 *
 * 001: should use seconds resolution to allow 2 packets through then fault.
 */

START_TEST (test_check_pass_001)
{
	pgm_rate_t rate;
	memset (&rate, 0, sizeof(rate));
	pgm_rate_create (&rate, 2*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	pgm_rate_destroy (&rate);
}
END_TEST

START_TEST (test_check_fail_001)
{
	pgm_rate_check (NULL, 1000, FALSE);
	fail ("reached");
}
END_TEST

/* 002: assert that only one packet should pass through small bucket 
 */

START_TEST (test_check_pass_002)
{
	pgm_rate_t rate;
	memset (&rate, 0, sizeof(rate));
	pgm_rate_create (&rate, 2*900, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	pgm_rate_destroy (&rate);
}
END_TEST

/* 003: millisecond resolution should initiate millisecond fills.
 */

START_TEST (test_check_pass_003)
{
	pgm_rate_t rate;
	memset (&rate, 0, sizeof(rate));
	pgm_rate_create (&rate, 2*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (TRUE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	fail_unless (FALSE == pgm_rate_check (&rate, 1000, TRUE), "rate_check failed");
	pgm_rate_destroy (&rate);
}
END_TEST

/* target:
 *	bool
 *	pgm_rate_check2 (
 *		pgm_rate_t*		major_bucket,
 *		pgm_rate_t*		minor_bucket,
 *		const size_t		data_size,
 *		const bool		is_nonblocking
 *	)
 *
 * 001: should use seconds resolution to allow 2 packets through then fault.
 */

START_TEST (test_check2_pass_001)
{
	pgm_rate_t major, minor;

/* major-only */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major#1 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major#2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major#3 failed");
	pgm_rate_destroy (&major);

/* minor-only */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&minor, 2*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	pgm_rate_destroy (&minor);

/* major with large minor */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010, 10, 1500);
	pgm_rate_create (&minor, 999*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/* minor with large major */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 999*1010, 10, 1500);
	pgm_rate_create (&minor, 2*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/* major and minor */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010, 10, 1500);
	pgm_rate_create (&minor, 2*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);
}
END_TEST

START_TEST (test_check2_fail_001)
{
	pgm_rate_check2 (NULL, NULL, 1000, FALSE);
	fail ("reached");
}
END_TEST

/* 002: assert that only one packet should pass through small bucket 
 */

START_TEST (test_check2_pass_002)
{
	pgm_rate_t major, minor;

/** major-only **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*900, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	pgm_rate_destroy (&major);

/* minor-only */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&minor, 2*900, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	pgm_rate_destroy (&minor);

/* major with large minor */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*900, 10, 1500);
	pgm_rate_create (&minor, 999*1010, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/* minor with large major */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 999*1010, 10, 1500);
	pgm_rate_create (&minor, 2*900, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/* major and minor */
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*900, 10, 1500);
	pgm_rate_create (&minor, 2*900, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);
}
END_TEST

/* 003: millisecond resolution should initiate millisecond fills.
 */

START_TEST (test_check2_pass_003)
{
	pgm_rate_t major, minor;

/** major-only **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:major failed");
	pgm_rate_destroy (&major);

/** minor-only **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&minor, 2*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:minor failed");
	pgm_rate_destroy (&minor);

/** major with large minor **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010*1000, 10, 1500);
	pgm_rate_create (&minor, 999*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1<2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/** minor with large major **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 999*1010*1000, 10, 1500);
	pgm_rate_create (&minor, 2*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:>>2 failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1>2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

/** major and minor **/
	memset (&major, 0, sizeof(major));
	memset (&minor, 0, sizeof(minor));
	mock_pgm_time_now = 1;
	pgm_rate_create (&major, 2*1010*1000, 10, 1500);
	pgm_rate_create (&minor, 2*1010*1000, 10, 1500);
	mock_pgm_time_now += pgm_secs(2);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
/* duplicate check at same time point */
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
/* advance time causing a millisecond fill to occur */
	mock_pgm_time_now += pgm_msecs(1);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
/* advance time to fill bucket enough for only one packet */
	mock_pgm_time_now += pgm_usecs(500);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
/* advance time to fill the bucket a little but not enough for one packet */
	mock_pgm_time_now += pgm_usecs(100);
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
/* advance time a lot, should be limited to millisecond fill rate */
	mock_pgm_time_now += pgm_secs(10);
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (TRUE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	fail_unless (FALSE == pgm_rate_check2 (&major, &minor, 1000, TRUE), "rate_check2:1=2 failed");
	pgm_rate_destroy (&major);
	pgm_rate_destroy (&minor);

}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_test (tc_create, test_create_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_create, test_create_fail_001, SIGABRT);
#endif

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_destroy, test_destroy_fail_001, SIGABRT);
#endif

	TCase* tc_check = tcase_create ("check");
	suite_add_tcase (s, tc_check);
	tcase_add_test (tc_check, test_check_pass_001);
	tcase_add_test (tc_check, test_check_pass_002);
	tcase_add_test (tc_check, test_check_pass_003);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_check, test_check_fail_001, SIGABRT);
#endif

	TCase* tc_check2 = tcase_create ("check2");
	suite_add_tcase (s, tc_check2);
	tcase_add_test (tc_check2, test_check2_pass_001);
	tcase_add_test (tc_check2, test_check2_pass_002);
	tcase_add_test (tc_check2, test_check2_pass_003);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_check2, test_check2_fail_001, SIGABRT);
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

/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for asynchronous queue.
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


#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#include <pgm/transport.h>


/* mock state */

static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
pgm_transport_t*
generate_transport (void)
{
	pgm_transport_t* transport = g_malloc0 (sizeof(pgm_transport_t));
	return transport;
}

static
struct pgm_msgv_t*
generate_msgv (void)
{
	const char source[] = "i am not a string";
	struct pgm_msgv_t* msgv = g_malloc0 (sizeof(struct pgm_msgv_t));
	msgv->msgv_len = sizeof(source);
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (1500);
	pgm_skb_put (skb, sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	msgv->msgv_skb[0] = skb;
	return msgv;
}


/* mock functions for external references */

static gpointer mock_msgv = NULL;

static
gssize
mock_pgm_transport_recvmsg (
	pgm_transport_t*	transport,
	pgm_msgv_t*		msgv,
	int			flags
	)
{
	pgm_msgv_t* _msgv = g_atomic_pointer_get (&mock_msgv);
	if (NULL == _msgv) {
		errno = EAGAIN;
		return -1;
	}
	memcpy (msgv, _msgv, sizeof(pgm_msgv_t));
	g_atomic_pointer_set (&mock_msgv, NULL);
	return _msgv->msgv_len;
}

#define pgm_transport_recvmsg	mock_pgm_transport_recvmsg

#define ASYNC_DEBUG
#include "async.c"


/* target:
 *	gboolean
 *	pgm_async_create (
 *		pgm_async_t**		async,
 *		pgm_transport_t*	transport,
 *		GError**		error
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_async_t* async = NULL;
	pgm_transport_t* transport = generate_transport ();
	GError* err = NULL;
	fail_unless (TRUE == pgm_async_create (&async, transport, &err));
}
END_TEST

START_TEST (test_create_fail_001)
{
	GError* err = NULL;
	fail_unless (FALSE == pgm_async_create (NULL, NULL, &err));
}
END_TEST

START_TEST (test_create_fail_002)
{
	pgm_async_t* async = NULL;
	GError* err = NULL;
	fail_unless (FALSE == pgm_async_create (&async, NULL, &err));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_async_destroy (
 *		pgm_async_t*		async
 *		)
 */

START_TEST (test_destroy_pass_001)
{
	pgm_async_t* async = NULL;
	pgm_transport_t* transport = generate_transport ();
	GError* err = NULL;
	fail_unless (TRUE == pgm_async_create (&async, transport, &err));
	fail_unless (TRUE == pgm_async_destroy (async));
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	fail_unless (FALSE == pgm_async_destroy (NULL));
}
END_TEST

/* target:
 *	GIOStatus
 *	pgm_async_recv (
 *		pgm_async_t*		async,
 *		gpointer		data,
 *		gsize			len,
 *		gsize*			bytes_read
 *		int			flags,
 *		GError**		error
 *		)
 */

START_TEST (test_recv_pass_001)
{
	pgm_async_t* async = NULL;
	pgm_transport_t* transport = generate_transport ();
	GError* err = NULL;
	fail_unless (TRUE == pgm_async_create (&async, transport, &err));
	struct pgm_msgv_t* msgv = generate_msgv ();
	g_atomic_pointer_set (&mock_msgv, msgv);
	char buffer[1024];
	gsize bytes_read = 0;
	fail_unless (G_IO_STATUS_NORMAL == pgm_async_recv (async, &buffer, sizeof(buffer), &bytes_read, 0, &err));
	fail_unless (TRUE == pgm_async_destroy (async));
	g_message ("recv returned \"%s\"", buffer);
}
END_TEST

START_TEST (test_recv_fail_001)
{
	fail_unless (G_IO_STATUS_ERROR == pgm_async_recv (NULL, NULL, 0, NULL, 0, NULL));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_async_create_watch (
 */

/* target:
 *	gboolean
 *	pgm_async_add_watch_full (
 */

/* target:
 *	gboolean
 *	pgm_async_add_watch (
 */

/* target:
 *	gboolean
 *	pgm_async_get_fd (
 */



static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_checked_fixture (tc_create, mock_setup, NULL);
	tcase_add_test (tc_create, test_create_pass_001);
	tcase_add_test (tc_create, test_create_fail_001);
	tcase_add_test (tc_create, test_create_fail_002);

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_checked_fixture (tc_destroy, mock_setup, NULL);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
	tcase_add_test (tc_destroy, test_destroy_fail_001);

	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, NULL);
	tcase_add_test (tc_recv, test_recv_pass_001);
	tcase_add_test (tc_recv, test_recv_fail_001);

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

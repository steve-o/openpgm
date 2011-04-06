/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM MIB routines.
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
#include <stdlib.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <glib.h>
#include <check.h>

#include "impl/framework.h"


/* mock state */
static pgm_rwlock_t     mock_pgm_sock_list_lock;
static pgm_slist_t*     mock_pgm_sock_list;

/* mock functions for external references */

#define pgm_sock_list_lock      mock_pgm_sock_list_lock
#define pgm_sock_list           mock_pgm_sock_list

static
netsnmp_handler_registration*
mock_netsnmp_create_handler_registration (
	const char*			name,
	Netsnmp_Node_Handler*		handler_access_method,
	oid*				reg_oid,
	size_t				reg_oid_len,
	int				modes
	)
{
	netsnmp_handler_registration* handler = g_malloc0 (sizeof(netsnmp_handler_registration));
	return handler;
}

static
void
mock_netsnmp_handler_registration_free (
	netsnmp_handler_registration*	handler
	)
{
	g_assert (NULL != handler);
	g_free (handler);
}

static
void
mock_netsnmp_table_helper_add_indexes (
	netsnmp_table_registration_info* tinfo,
	...
	)
{
}

static
int
mock_netsnmp_register_table_iterator (
	netsnmp_handler_registration*	reginfo,
	netsnmp_iterator_info*		iinfo
	)
{
	return MIB_REGISTERED_OK;
}

static
int
mock_netsnmp_set_request_error (
	netsnmp_agent_request_info*	reqinfo,
	netsnmp_request_info*		request,
	int				error_value
	)
{
	return 0;
}

static
void*
mock_netsnmp_extract_iterator_context (
	netsnmp_request_info*		reqinfo
	)
{
	return (void*)0x1;
}

static
netsnmp_table_request_info*
mock_netsnmp_extract_table_info (
	netsnmp_request_info*		reqinfo
	)
{
	return NULL;
}

static
int
mock_snmp_set_var_typed_value (
	netsnmp_variable_list*		newvar,
	u_char				type,
	const u_char*			val_str,
	size_t				val_len
	)
{
	return 0;
}

static
netsnmp_variable_list*
mock_snmp_varlist_add_variable (
	netsnmp_variable_list**		varlist,
	const oid*			oid,
	size_t				name_length,
	u_char				type,
	const u_char*			value,
	size_t				len
	)
{
	return NULL;
}

static
void
mock_snmp_free_varbind (
	netsnmp_variable_list*		var
	)
{
}

static
void
mock_snmp_free_var (
	netsnmp_variable_list*		var
	)
{
}

static
int
mock_snmp_log (
	int				priority,
	const char*			format,
	...
	)
{
	return 0;
}

static
void
mock_send_v2trap (
	netsnmp_variable_list*		var
	)
{
}

/** time module */

static
void
mock_pgm_time_since_epoch (
	pgm_time_t*	pgm_time_t_time,
	time_t*		time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time + 0);
}


#define netsnmp_create_handler_registration	mock_netsnmp_create_handler_registration
#define netsnmp_handler_registration_free	mock_netsnmp_handler_registration_free
#define netsnmp_table_helper_add_indexes	mock_netsnmp_table_helper_add_indexes
#define netsnmp_register_table_iterator		mock_netsnmp_register_table_iterator
#define netsnmp_set_request_error		mock_netsnmp_set_request_error
#define netsnmp_extract_iterator_context	mock_netsnmp_extract_iterator_context
#define netsnmp_extract_table_info		mock_netsnmp_extract_table_info
#define snmp_set_var_typed_value		mock_snmp_set_var_typed_value
#define snmp_varlist_add_variable		mock_snmp_varlist_add_variable
#define snmp_free_varbind			mock_snmp_free_varbind
#define snmp_free_var				mock_snmp_free_var
#define snmp_log				mock_snmp_log
#define send_v2trap				mock_send_v2trap

#define PGMMIB_DEBUG
#include "pgmMIB.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	bool
 *	pgm_mib_init (
 *		pgm_error_t**		error
 *	)
 */

START_TEST (test_init_pass_001)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_mib_init (&err), "mib_init failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init = tcase_create ("init");
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_init_pass_001);
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

/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for global session ID helper functions.
 *
 * Copyright (c) 2009-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include "mock-error.hh"
#include "mock-md5.hh"
#include "mock-messages.hh"
#include "mock-rand.hh"
#include "mock-runtime.hh"
#include "mock-wsastrerror.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"


/* mock state */

static const char* hostname_localhost = "localhost";
static const char* hostname_invalid = "invalid.invalid";		/* RFC 2606 */
static const char* hostname_toolong = "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij12345"; /* 65 */


namespace under_test
{
	Runtime* crt;
	Pgm::internal::Error* error;
	Pgm::internal::Md5* md5;
	Pgm::internal::Messages* messages;
	Pgm::internal::Rand* rand;

	int gethostname (char *name, size_t len) {
		return crt->gethostname (name, len);
	}

	void pgm_set_error (struct pgm_error_t** err, int error_domain, int error_code, const char* format, va_list args) {
		return error->pgm_set_error (reinterpret_cast<struct ::pgm_error_t**>( err ), error_domain, error_code, format, args);
	}
	int pgm_error_from_errno (int errnum) {
		return error->pgm_error_from_errno (errnum);
	}
	int pgm_error_from_eai_errno (int eai_errnum, int errnum) {
		return error->pgm_error_from_eai_errno (eai_errnum, errnum);
	}
	int pgm_error_from_wsa_errno (int wsa_errnum) {
		return error->pgm_error_from_wsa_errno (wsa_errnum);
	}

	void pgm_md5_init_ctx (struct pgm_md5_t* ctx) {
		return md5->pgm_md5_init_ctx (reinterpret_cast<Pgm::internal::pgm_md5_t*>( ctx ));
	}
        void pgm_md5_process_bytes (struct pgm_md5_t* ctx, const void* buffer, size_t len) {
		return md5->pgm_md5_process_bytes (reinterpret_cast<Pgm::internal::pgm_md5_t*>( ctx ), buffer, len);
	}
        void* pgm_md5_finish_ctx (struct pgm_md5_t* ctx, void* resbuf) {
		return md5->pgm_md5_finish_ctx (reinterpret_cast<Pgm::internal::pgm_md5_t*>( ctx ), resbuf);
	}

	void pgm__logv (int log_level, const char* format, va_list args) {
		return messages->pgm__logv (log_level, format, args);
	}
// Route onto va_list API
	void pgm__log (int log_level, const char* format, ...) {
		va_list args;
		va_start (args, format);
		pgm__logv (log_level, format, args);
		va_end (args);
	}

	int32_t pgm_random_int_range (int32_t begin, int32_t end) {
		return rand->pgm_random_int_range (begin, end);
	}

// Route direct to real API
	char* pgm_wsastrerror (int wsa_errnum) {
		return ::pgm_wsastrerror (wsa_errnum);
	}

#define GSI_DEBUG
#include "gsi.c"

	int pgm_min_log_level = PGM_LOG_LEVEL_TRACE;
}

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::NotNull;
using ::testing::NiceMock;
using namespace ::under_test;

std::ostream& operator << (std::ostream& os, const pgm_error_t*& err)
{
	os << "{ domain=" << err->domain << " code=" << err->code << " message=";
	if (NULL == err->message)
		os << err->message;
	else
		os << '"' << err->message << '"';
	os << " }";
	return os;
}

/* Set the hostname returned by mocked gethostname() API.
 *
 * glibc 2.2 truncates and returns -1/ENAMETOOLONG.
 * glibc 2.1 returns -1/ENAMETOOLONG only.
 * glibc 2.0 returns -1/EINVAL only.
 * POSIX no error is returned, name is silently truncated.
 * WinSock returns -1/WSAEFAULT only.
 *
 * Worst case is POSIX but lets be nice and return an error if the buffer
 * is sufficient per RFC1034 host name limit of 63 bytes.
 *
 * Note no guarantees are provided on trailing null byte.
 */

ACTION_P(SetHostname, hostname)
{
	if (strlen (hostname) > 63) /* HOST_NAME_MAX */
	{
#ifndef _WIN32
		errno = ENAMETOOLONG;
#else
		WSASetLastError (WSAEFAULT);
#endif
		return -1;
	}

/* silent truncation for short buffers */
	strncpy (arg0, hostname, arg1);
	return 0;
}

/* target:
 *	bool
 *	pgm_gsi_create_from_hostname (
 *		pgm_gsi_t*		gsi,
 *		pgm_error_t**		err
 *	)
 */

TEST (GsiFromHostnameTest, HandlesValidInput)
{
	MockRuntime crt;
	NiceMock<Pgm::internal::MockError>	nice_error;
	NiceMock<Pgm::internal::MockMd5>	nice_md5;
	Pgm::internal::FakeMessages		messages;

	EXPECT_CALL (crt, gethostname (_, _))
		.Times (2)
		.WillRepeatedly (SetHostname (hostname_localhost));

	::crt = &crt; ::error = &nice_error; ::md5 = &nice_md5; ::messages = &messages;

	pgm_gsi_t gsi;
	pgm_error_t* err = NULL;
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &gsi, &err) << "err=" << err;
	ASSERT_EQ (NULL, err);
	EXPECT_PRED2 (pgm_gsi_create_from_hostname, &gsi, (pgm_error_t**)NULL);
}

TEST (GsiFromHostnameTest, HandlesNullInput)
{
	MockRuntime crt;
	NiceMock<Pgm::internal::MockError>	nice_error;
	NiceMock<Pgm::internal::MockMd5>	nice_md5;
	NiceMock<Pgm::internal::MockMessages>	nice_messages;

	::crt = &crt; ::error = &nice_error; ::md5 = &nice_md5; ::messages = &nice_messages;

	pgm_error_t* err = NULL;
	ASSERT_FALSE (pgm_gsi_create_from_hostname (NULL, &err)) << "err=" << err;
	EXPECT_EQ (NULL, err);
	EXPECT_FALSE (pgm_gsi_create_from_hostname (NULL, NULL));
}

TEST (GsiFromHostnameTest, HandlesHostnameTooLong)
{
	MockRuntime crt;
	Pgm::internal::RealError		real_error;
	NiceMock<Pgm::internal::MockMd5>	nice_md5;
	NiceMock<Pgm::internal::MockMessages>	nice_messages;

	EXPECT_CALL (crt, gethostname (_, _))
		.Times (2)
		.WillRepeatedly (SetHostname (hostname_toolong));

	::crt = &crt; ::error = &real_error; ::md5 = &nice_md5; ::messages = &nice_messages;

	pgm_gsi_t gsi;
	pgm_error_t* err = NULL;
	ASSERT_FALSE (pgm_gsi_create_from_hostname (&gsi, &err)) << "err=" << err;
	ASSERT_THAT (err, NotNull());
	ASSERT_THAT (err->message, NotNull());
	EXPECT_FALSE (pgm_gsi_create_from_hostname (&gsi, NULL));
}

/* target:
 *	bool
 *	pgm_gsi_create_from_addr (
 *		pgm_gsi_t*		gsi,
 *		pgm_error_t**		err
 *	)
 */

#if 0
TEST (GsiFromAddressTest, HandlesValidInput)
{
	pgm_gsi_t gsi;
	pgm_error_t* err = NULL;
	ASSERT_PRED2 (pgm_gsi_create_from_addr, &gsi, &err) << "Failed with message: " << err->message;
	ASSERT_EQ (NULL, err);
	EXPECT_PRED2 (pgm_gsi_create_from_addr, &gsi, NULL);
}

TEST (GsiFromAddressTest, HandlesNullInput)
{
	pgm_error_t* err = NULL;
	ASSERT_FALSE (pgm_gsi_create_from_addr (NULL, &err)) << "Ignored null input";
	EXPECT_EQ (NULL, err) << "Error object returned from null input: " << err->message;
	EXPECT_FALSE (pgm_gsi_create_from_addr (NULL, NULL));
}

TEST (GsiFromAddressTest, HandlesInvalidHostname)
{
	pgm_gsi_t gsi;
	pgm_error_t* err = NULL;
	ASSERT_FALSE (pgm_gsi_create_from_addr (&gsi, &err)) << "Ingored invalid hostname";
	ASSERT_NE (NULL, err);
	ASSERT_NE (NULL, err->message);
	g_debug ("pgm_error_t: %s", err->message);
	EXPECT_FALSE (pgm_gsi_create_from_addr (&gsi, NULL));
}

/* target:
 *	char*
 *	pgm_gsi_print (
 *		const pgm_gsi_t*	gsi
 *	)
 */

TEST (GsiPrintTest, HandlesValidInput)
{
	pgm_gsi_t gsi;
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &gsi, NULL);
	EXPECT_NE (NULL, pgm_gsi_print (&gsi));
}

TEST (GsiPrintTest, HandlesNullInput)
{
	EXPECT_EQ (NULL, pgm_gsi_print (NULL));
}

/* target:
 *	int
 *	pgm_gsi_print_r (
 *		const pgm_gsi_t*	gsi,
 *		char*			buf,
 *		size_t			bufsize
 *	)
 */

TEST (GsiReentrantPrintTest, HandlesValidInput)
{
	pgm_gsi_t gsi;
	char buf[PGM_GSISTRLEN];
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &gsi, NULL);
	EXPECT_GT (pgm_gsi_print_r (&gsi, buf, sizeof (buf)), 0);
}

TEST (GsiReentrantPrintTest, HandlesNullInput)
{
	pgm_gsi_t gsi;
	char buf[PGM_GSISTRLEN];
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &gsi, NULL);
	EXPECT_EQ (-1, pgm_gsi_print_r (NULL, buf, sizeof (buf)));
	EXPECT_EQ (-1, pgm_gsi_print_r (&gsi, NULL, sizeof (buf)));
	EXPECT_EQ (-1, pgm_gsi_print_r (&gsi, buf, 0));
}

/* target:
 *	bool
 *	pgm_gsi_equal (
 *		const void*	gsi1,
 *		const void*	gsi2
 *	)
 */

TEST (GsiEqualTest, HandlesValidInput)
{
	pgm_gsi_t hostname1, hostname2, address1, address2;
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &hostname1, NULL);
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &hostname2, NULL);
	ASSERT_PRED2 (pgm_gsi_create_from_addr, &address1, NULL);
	ASSERT_PRED2 (pgm_gsi_create_from_addr, &address2, NULL);
/* Ensure no "restrict" usage for assuming pointers must be different. */
	EXPECT_PRED2 (pgm_gsi_equal, &hostname1, &hostname1);
	EXPECT_PRED2 (pgm_gsi_equal, &hostname1, &hostname2);
	EXPECT_PRED2 (pgm_gsi_equal, &address1, &address1);
	EXPECT_PRED2 (pgm_gsi_equal, &address1, &address2);
	EXPECT_FALSE (pgm_gsi_equal (&hostname1, &address1));
}

TEST (GsiEqualTest, HandlesNullInput)
{
	pgm_gsi_t gsi;
	ASSERT_PRED2 (pgm_gsi_create_from_hostname, &gsi, NULL);
	EXPECT_FALSE (pgm_gsi_equal (NULL, &gsi));
	ADD_FAILURE();
	EXPECT_FALSE (pgm_gsi_equal (&gsi, NULL));
	ADD_FAILURE();
}
#endif

#ifdef _WIN32
class WinSockEnvironment : public testing::Environment {
	virtual ~WinSockEnvironment() {}

	virtual void SetUp() {
		WORD wVersionRequested = MAKEWORD (2, 2);
		WSADATA wsaData;
		assert (0 == WSAStartup (wVersionRequested, &wsaData));
		assert (LOBYTE (wsaData.wVersion) == 2 && HIBYTE (wsaData.wVersion) == 2);
	}

	virtual void TearDown() {
		WSACleanup();
	}
};
#endif

class Environment : public testing::Environment {
protected:
	MockRuntime			crt;
	Pgm::internal::MockError	error;
	Pgm::internal::MockMd5		md5;
	Pgm::internal::FakeMessages	messages;

public:
	virtual ~Environment() {}

	virtual void SetUp() {
		under_test::crt		= &crt;
		under_test::error	= &error;
		under_test::md5		= &md5;
		under_test::messages	= &messages;
	}

	virtual void TearDown() {
		under_test::crt		= NULL;
		under_test::error	= NULL;
		under_test::md5		= NULL;
		under_test::messages	= NULL;
	}
};

int
main (int argc, char** argv)
{
	::testing::InitGoogleMock (&argc, argv);
#ifdef _WIN32
	::testing::AddGlobalTestEnvironment (new WinSockEnvironment);
#endif
	::testing::AddGlobalTestEnvironment (new Environment);
	return RUN_ALL_TESTS();
}

/* eof */

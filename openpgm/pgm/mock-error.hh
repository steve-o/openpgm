/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Mocked error API
 *
 * Copyright (c) 2011 Miru Limited.
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

#include <stdarg.h>
#include "gmock/gmock.h"

/* under test */
#include "pgm/error.h"

namespace Pgm
{

#if 0
/* error domains */
enum
{
	PGM_ERROR_DOMAIN_IF,		/* interface and host */
	PGM_ERROR_DOMAIN_PACKET,
	PGM_ERROR_DOMAIN_RECV,
	PGM_ERROR_DOMAIN_TIME,
	PGM_ERROR_DOMAIN_SOCKET,
	PGM_ERROR_DOMAIN_ENGINE,
	PGM_ERROR_DOMAIN_HTTP,
	PGM_ERROR_DOMAIN_SNMP
};

/* error codes */
enum
{
	/* Derived from errno, eai_errno, etc */
	PGM_ERROR_ADDRFAMILY,		/* EAI_ADDRFAMILY */
	PGM_ERROR_AFNOSUPPORT,		/* EAI_FAMILY */
	PGM_ERROR_AGAIN,
	PGM_ERROR_BADE,			/* ERROR_INVALID_DATA */
	PGM_ERROR_BADF,
	PGM_ERROR_BOUNDS,		/* sequence out-of-bounds */
	PGM_ERROR_CKSUM,		/* pkt cksum incorrect */
	PGM_ERROR_CONNRESET,
	PGM_ERROR_FAIL,			/* EAI_FAIL */
	PGM_ERROR_FAULT,
	PGM_ERROR_INPROGRESS,		/* WSAEINPROGRESS */
	PGM_ERROR_INTR,
	PGM_ERROR_INVAL,
	PGM_ERROR_MFILE,
	PGM_ERROR_NFILE,
	PGM_ERROR_NOBUFS,		/* ERROR_BUFFER_OVERFLOW */
	PGM_ERROR_NODATA,		/* EAI_NODATA */
	PGM_ERROR_NODEV,
	PGM_ERROR_NOENT,
	PGM_ERROR_NOMEM,
	PGM_ERROR_NONAME,		/* EAI_NONAME */
	PGM_ERROR_NONET,
	PGM_ERROR_NOPROTOOPT,
	PGM_ERROR_NOSYS,		/* ERROR_NOT_SUPPORTED */
	PGM_ERROR_NOTUNIQ,
	PGM_ERROR_NXIO,
	PGM_ERROR_PERM,
	PGM_ERROR_PROCLIM,		/* WSAEPROCLIM */
	PGM_ERROR_PROTO,
	PGM_ERROR_RANGE,
	PGM_ERROR_SERVICE,		/* EAI_SERVICE */
	PGM_ERROR_SOCKTNOSUPPORT,	/* EAI_SOCKTYPE */
	PGM_ERROR_SYSNOTAREADY,		/* WSASYSNOTAREADY */
	PGM_ERROR_SYSTEM,		/* EAI_SYSTEM */
	PGM_ERROR_VERNOTSUPPORTED,	/* WSAVERNOTSUPPORTED */
	PGM_ERROR_XDEV,

	PGM_ERROR_FAILED		/* generic error */
};

struct pgm_error_t
{
	int		domain;
	int		code;
	char*		message;
};

namespace external
{

void pgm_error_free (struct pgm_error_t*);
void pgm_set_error (struct pgm_error_t**, const int, const int, const char*, ...);
void pgm_propagate_error (struct pgm_error_t**, struct pgm_error_t*);
void pgm_clear_error (struct pgm_error_t**);
void pgm_prefix_error (struct pgm_error_t**, const char*, ...);

int pgm_error_from_errno (const int);
int pgm_error_from_h_errno (const int);
int pgm_error_from_eai_errno (const int, const int);
int pgm_error_from_wsa_errno (const int);
int pgm_error_from_win_errno (const int);

}; /* namespace external */
#endif

namespace internal
{

class Error {
public:
	virtual ~Error() {}
//	virtual void pgm_set_error (struct pgm_error_t** err, int error_domain, int error_code, const char* format, ...) = 0;
	virtual void pgm_set_error (struct pgm_error_t** err, int error_domain, int error_code, const char* format, va_list args) = 0;
	virtual int pgm_error_from_errno (int errnum) = 0;
	virtual int pgm_error_from_eai_errno (int eai_errnum, int errnum) = 0;
	virtual int pgm_error_from_wsa_errno (int wsa_errnum) = 0;
};

class RealError : public Error {
public:
	void pgm_set_error (struct pgm_error_t** err, int error_domain, int error_code, const char* format, va_list args) {
		return ::pgm_set_error (reinterpret_cast<struct ::pgm_error_t**>( err ), error_domain, error_code, format, args);
	}
	int pgm_error_from_errno (int errnum) {
		return ::pgm_error_from_errno (errnum);
	}
	int pgm_error_from_eai_errno (int eai_errnum, int errnum) {
		return ::pgm_error_from_eai_errno (eai_errnum, errnum);
	}
	int pgm_error_from_wsa_errno (int wsa_errnum) {
		return ::pgm_error_from_wsa_errno (wsa_errnum);
	}
};

class FakeError : public Error {
public:
	void pgm_set_error (struct pgm_error_t** err, int error_domain, int error_code, const char* format, va_list args) {
		if (NULL == err)
			return;
		assert (NULL != *err);
		*err = (struct pgm_error_t*)malloc (sizeof (::pgm_error_t));
		(*err)->domain  = error_domain;
		(*err)->code    = error_code;
		(*err)->message = strdup ("insert error message");
	}
	int pgm_error_from_errno (int errnum) {
		return PGM_ERROR_FAILED;
	}
	int pgm_error_from_eai_errno (int eai_errnum, int errnum) {
		return PGM_ERROR_FAILED;
	}
	int pgm_error_from_wsa_errno (int wsa_errnum) {
		return PGM_ERROR_FAILED;
	}
};

class MockError : public Error {
public:
	MOCK_METHOD5 (pgm_set_error, void (struct pgm_error_t** err, int error_domain, int error_code, const char* format, va_list args));
	MOCK_METHOD1 (pgm_error_from_errno, int (int errnum));
	MOCK_METHOD2 (pgm_error_from_eai_errno, int (int eai_errnum, int errnum));
	MOCK_METHOD1 (pgm_error_from_wsa_errno, int (int wsa_errnum));
};

} /* namespace internal */
} /* namespace Pgm */

/* eof */

/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Mocked MD5 API
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

#include "gmock/gmock.h"

namespace Pgm
{
namespace internal
{

class Md5 {
public:
	virtual ~Md5() {}
	virtual void pgm_md5_init_ctx (struct pgm_md5_t* ctx) = 0;
	virtual void pgm_md5_process_bytes (struct pgm_md5_t* ctx, const void* buffer, size_t len) = 0;
	virtual void* pgm_md5_finish_ctx (struct pgm_md5_t* ctx, void* resbuf) = 0;
};

class MockMd5 : public Md5 {
public:
	MOCK_METHOD1 (pgm_md5_init_ctx, void (struct pgm_md5_t* ctx));
	MOCK_METHOD3 (pgm_md5_process_bytes, void (struct pgm_md5_t* ctx, const void* buffer, size_t len));
	MOCK_METHOD2 (pgm_md5_finish_ctx, void* (struct pgm_md5_t* ctx, void* resbuf));
};

} /* namespace internal */
} /* namespace Pgm */

/* eof */

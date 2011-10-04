/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Mocked PRNG API
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

class Rand {
public:
	virtual ~Rand() {}
	virtual int32_t pgm_random_int_range (int32_t begin, int32_t end) = 0;
};

class MockRand : public Rand {
public:
	MOCK_METHOD2 (pgm_random_int_range, int32_t (int32_t begin, int32_t end));
};

} /* namespace internal */
} /* namespace Pgm */

/* eof */

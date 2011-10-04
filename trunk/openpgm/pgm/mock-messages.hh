/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Mocked messages API
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

namespace Pgm
{
namespace internal
{

class Messages {
public:
	virtual ~Messages() {}
//	virtual void pgm__log (int log_level, const char* format, ...);
	virtual void pgm__logv (int log_level, const char* format, va_list args) = 0;
};

class FakeMessages : public Messages {
public:
	void pgm__logv (int log_level, const char* format, va_list args) {
		vprintf (format, args);
		putchar ('\n');
	}
};

class MockMessages : public Messages {
public:
	MOCK_METHOD3 (pgm__logv, void (int log_level, const char* format, va_list args));
};

} /* namespace internal */
} /* namespace Pgm */

/* eof */

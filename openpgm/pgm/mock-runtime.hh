/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Mocked C run-time library API
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

class Runtime {
public:
	virtual ~Runtime() {}
	virtual int gethostname (char *name, size_t len) = 0;
};

class MockRuntime : public Runtime {
public:
	MOCK_METHOD2 (gethostname, int (char *name, size_t len));
};

/* eof */

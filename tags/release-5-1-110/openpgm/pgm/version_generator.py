#!/usr/bin/python

import os
import platform
import time

build_date = time.strftime ("%Y-%m-%d")
build_time = time.strftime ("%H:%M:%S")
build_rev = os.popen('svnversion -n .').read();

print """
/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * OpenPGM version.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#include <impl/framework.h>
#include <pgm/version.h>


/* globals */

const unsigned pgm_major_version = 5;
const unsigned pgm_minor_version = 1;
const unsigned pgm_micro_version = 110;
const char* pgm_build_date = "%s";
const char* pgm_build_time = "%s";
const char* pgm_build_system = "%s";
const char* pgm_build_machine = "%s";
const char* pgm_build_revision = "%s";


/* eof */
"""%(build_date, build_time, platform.system(), platform.machine(), build_rev)

# end of file

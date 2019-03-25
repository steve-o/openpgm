/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * CPUID wrapper.
 *
 * Copyright (c) 2016 Miru Limited.
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
#	include <config.h>
#endif

#if defined(_MSC_VER)
#	include <intrin.h>
#	include <immintrin.h>  // For _xgetbv()
#endif

#include <impl/framework.h>

//#define CPU_DEBUG


#ifndef _MSC_VER
static
void
__cpuidex (int cpu_info[4], int function_id, int subfunction_id) {
  __asm__ volatile (
    "mov %%ebx, %%edi\n"
    "cpuid\n"
    "xchg %%edi, %%ebx\n"
    : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]), "=d"(cpu_info[3])
    : "a"(function_id), "c"(subfunction_id)
  );
}

// _xgetbv returns the value of an Intel Extended Control Register (XCR).
// Currently only XCR0 is defined by Intel so |xcr| should always be zero.
static
uint64_t
_xgetbv(uint32_t xcr) {
  uint32_t eax, edx;
  __asm__ volatile (
    "xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
  return (((uint64_t)(edx)) << 32) | eax;
}
#endif


#if defined(__i386__) || defined(__x86_64__)
PGM_GNUC_INTERNAL
void
pgm_cpuid (pgm_cpu_t* cpu)
{
	memset (cpu, 0, sizeof (pgm_cpu_t));

	int cpu_info[4] = {-1};
// Calling __cpuid with 0x0 as the function_id argument
// gets the number of the highest valid function ID.
	__cpuidex (cpu_info, 0x0, 0x0);
	const int num_ids = cpu_info[0];
	if (num_ids == 0) {
// no valid ids
		return;
	}
	int cpu_info7[4] = {0};
	__cpuidex (cpu_info, 0x1, 0x0);
	if (num_ids >= 7) {
		__cpuidex (cpu_info7, 0x7, 0x0);
	}

	cpu->has_mmx =   (cpu_info[3] & 0x00800000) != 0;
	cpu->has_sse =   (cpu_info[3] & 0x02000000) != 0;
	cpu->has_sse2 =  (cpu_info[3] & 0x04000000) != 0;
	cpu->has_sse3 =  (cpu_info[2] & 0x00000001) != 0;
	cpu->has_ssse3 = (cpu_info[2] & 0x00000200) != 0;
	cpu->has_sse41 = (cpu_info[2] & 0x00080000) != 0;
	cpu->has_sse42 = (cpu_info[2] & 0x00100000) != 0;
	cpu->has_avx = (cpu_info[2] & 0x10000000) != 0 &&
			(cpu_info[2] & 0x04000000) != 0 /* XSAVE */ &&
			(cpu_info[2] & 0x08000000) != 0 /* OSXSAVE */ &&
			(_xgetbv(0) & 6) == 6 /* XSAVE enabled by kernel */;
	cpu->has_avx2 = cpu->has_avx && (cpu_info7[1] & 0x00000020) != 0;
}
#else
PGM_GNUC_INTERNAL
void
pgm_cpuid (pgm_cpu_t* cpu)
{
	memset(cpu, 0, sizeof(pgm_cpu_t));
}
#endif

/* eof */

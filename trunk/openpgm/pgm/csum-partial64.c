/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Partial checksums 64 bit.
 *
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING.GPL in the main directory of this archive
 * for more details. No warranty for anything given at all.
 */

#include <glib.h>

#include "pgm/checksum.h"

static inline unsigned short from32to16(unsigned a)
{
        unsigned short b = a >> 16;
        asm("addw %w2,%w0\n\t"
            "adcw $0,%w0\n"
            : "=r" (b)
            : "0" (b), "r" (a));
        return b;
}

unsigned pgm_asm64_csum_partial(const unsigned char *buff, unsigned len)
{
        unsigned odd, count;
        unsigned long result = 0;

        if (G_UNLIKELY(len == 0)) {
                return result;
	}
        odd = 1 & (unsigned long) buff;
        if (G_UNLIKELY(odd)) {
                result = *buff << 8;
                len--;
                buff++;
        }
        count = len >> 1;               /* nr of 16-bit words.. */
        if (count) {
                if (2 & (unsigned long) buff) {
                        result += *(unsigned short *)buff;
                        count--;
                        len -= 2;
                        buff += 2;
                }
                count >>= 1;            /* nr of 32-bit words.. */
                if (count) {
                        unsigned long zero;
                        unsigned count64;
                        if (4 & (unsigned long) buff) {
                                result += *(unsigned int *) buff;
                                count--;
                                len -= 4;
                                buff += 4;
                        }
                        count >>= 1;    /* nr of 64-bit words.. */

                        /* main loop using 64byte blocks */
                        zero = 0;
                        count64 = count >> 3;
                        while (count64) {
                                asm("addq 0*8(%[src]),%[res]\n\t"
                                    "adcq 1*8(%[src]),%[res]\n\t"
                                    "adcq 2*8(%[src]),%[res]\n\t"
                                    "adcq 3*8(%[src]),%[res]\n\t"
                                    "adcq 4*8(%[src]),%[res]\n\t"
                                    "adcq 5*8(%[src]),%[res]\n\t"
                                    "adcq 6*8(%[src]),%[res]\n\t"
                                    "adcq 7*8(%[src]),%[res]\n\t"
                                    "adcq %[zero],%[res]"
                                    : [res] "=r" (result)
                                    : [src] "r" (buff), [zero] "r" (zero),
                                    "[res]" (result));
                                buff += 64;
                                count64--;
                        }

                        /* last upto 7 8byte blocks */
                        count %= 8;
                        while (count) {
                                asm("addq %1,%0\n\t"
                                    "adcq %2,%0\n"
                                            : "=r" (result)
                                    : "m" (*(unsigned long *)buff),
                                    "r" (zero),  "0" (result));
                                --count;
                                        buff += 8;
                        }
                        result = add32_with_carry(result>>32,
                                                  result&0xffffffff);

                        if (len & 4) {
                                result += *(unsigned int *) buff;
                                buff += 4;
                        }
                }
                if (len & 2) {
                        result += *(unsigned short *) buff;
                        buff += 2;
                }
        }
        if (len & 1)
                result += *buff;
        result = add32_with_carry(result>>32, result & 0xffffffff);
        if (G_UNLIKELY(odd)) {
                result = from32to16(result);
                result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
        }
        return result;
}

/* eof */


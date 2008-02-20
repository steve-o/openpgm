/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Checksums 32/64 bit.
 *
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details. No warranty for anything given at all.
 */

#ifndef __PGM_CKSUM_COPY_H__
#define __PGM_CKSUM_COPY_H__

#include <glib.h>


G_BEGIN_DECLS

static inline unsigned add32_with_carry (unsigned a, unsigned b)
{
    asm("addl %2,%0\n\t"
            "adcl $0,%0"
            : "=r" (a)
            : "0" (a), "r" (b));
        return a;
}

unsigned do_csum(const unsigned char *buff, unsigned len);

static inline guint32 csum_partial(const void *buff, int len, guint32 sum)
{
        return (guint32)add32_with_carry(do_csum(buff, len), sum);
}

guint32 csum_partial_copy_generic (const unsigned char *src, const unsigned char *dst,
                                         unsigned len,
                                         unsigned sum, 
                                         int *src_err_ptr, int *dst_err_ptr);

static inline guint32 csum_partial_copy_nocheck (const unsigned char *src, const unsigned char *dst, unsigned len, unsigned sum)
{
    return csum_partial_copy_generic (src, dst, len, sum, NULL, NULL);
}

G_END_DECLS

#endif /* __PGM_CKSUM_COPY_H__ */


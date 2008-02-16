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

guint32 csum_partial (const void *buff, int len, guint32 sum);

guint32 csum_partial_copy_generic (const unsigned char *src, const unsigned char *dst,
                                         unsigned len,
                                         unsigned sum, 
                                         int *src_err_ptr, int *dst_err_ptr);

static inline guint32 csum_add (guint32 csum, guint32 addend)
{
    guint32 res = csum;
    res += addend;
    return (guint32)(res + (res < addend));
}

static inline guint32 csum_block_add (guint32 csum, guint32 csum2, int offset)
{
    guint32 sum = csum2;
    if (offset&1)
	sum = ((sum&0xFF00FF)<<8)+((sum>>8)&0xFF00FF);
    return csum_add (csum, sum);
}

static inline guint16 csum_fold (guint32 sum)
{
    __asm__(
	    "  addl %1,%0\n"
	    "  adcl $0xffff,%0"
	    : "=r" (sum)
	    : "r" ((guint32)sum << 16),
	      "0" ((guint32)sum & 0xffff0000)
    );
    return (guint16)(~(guint32)sum >> 16);
}

G_END_DECLS

#endif /* __PGM_CKSUM_COPY_H__ */


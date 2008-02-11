/*
 * lib/reed_solomon/encode_rs.c
 *
 * Overview:
 *   Generic Reed Solomon encoder / decoder library
 *
 * Copyright 2002, Phil Karn, KA9Q
 * May be used under the terms of the GNU General Public License (GPL)
 *
 * Adaption to the kernel by Thomas Gleixner (tglx@linutronix.de)
 *
 * $Id: encode_rs.c,v 1.5 2005/11/07 11:14:59 gleixner Exp $
 *
 */

/* Generic data width independent code which is included by the
 * wrappers.
 * int encode_rsX (struct rs_control *rs, uintX_t *data, int len, uintY_t *par)
 */
{
	int i, j, pad;
	int nn = rs->nn;
	int nroots = rs->nroots;
	guint16 *alpha_to = rs->alpha_to;
	guint16 *index_of = rs->index_of;
	guint16 *genpoly = rs->genpoly;
	guint16 fb;
	guint16 msk = (guint16) rs->nn;

	/* Check length parameter for validity */
	pad = nn - nroots - len;
	if (pad < 0 || pad >= nn)
		return -ERANGE;

	for (i = 0; i < len; i++) {
		fb = index_of[((((guint16) data[i])^invmsk) & msk) ^ par[0]];
		/* feedback term is non-zero */
		if (fb != nn) {
			for (j = 1; j < nroots; j++) {
				par[j] ^= alpha_to[rs_modnn(rs, fb +
							 genpoly[nroots - j])];
			}
		}
		/* Shift */
		memmove(&par[0], &par[1], sizeof(guint16) * (nroots - 1));
		if (fb != nn) {
			par[nroots - 1] = alpha_to[rs_modnn(rs,
							    fb + genpoly[0])];
		} else {
			par[nroots - 1] = 0;
		}
	}
	return 0;
}

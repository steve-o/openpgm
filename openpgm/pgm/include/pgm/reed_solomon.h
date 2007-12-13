/*
 * include/linux/rslib.h
 *
 * Overview:
 *   Generic Reed Solomon encoder / decoder library
 *
 * Copyright (C) 2004 Thomas Gleixner (tglx@linutronix.de)
 *
 * RS code lifted from reed solomon library written by Phil Karn
 * Copyright 2002 Phil Karn, KA9Q
 *
 * $Id: rslib.h,v 1.4 2005/11/07 11:14:52 gleixner Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PGM_REED_SOLOMON_H__
#define __PGM_REED_SOLOMON_H__

#include <glib.h>

/**
 * struct rs_control - rs control structure
 *
 * @mm:		Bits per symbol
 * @nn:		Symbols per block (= (1<<mm)-1)
 * @alpha_to:	log lookup table
 * @index_of:	Antilog lookup table
 * @genpoly:	Generator polynomial
 * @nroots:	Number of generator roots = number of parity symbols
 * @fcr:	First consecutive root, index form
 * @prim:	Primitive element, index form
 * @iprim:	prim-th root of 1, index form
 * @gfpoly:	The primitive generator polynominal
 * @users:	Users of this structure
 * @list:	List entry for the rs control list
*/
struct rs_control {
	int 		mm;
	int 		nn;
	guint16*	alpha_to;
	guint16*	index_of;
	guint16*	genpoly;
	int 		nroots;
	int 		fcr;
	int 		prim;
	int 		iprim;
	int		gfpoly;
	int		users;

	GList*		link;
};

/* General purpose RS codec, 8-bit data width, symbol width 1-15 bit  */
#ifdef CONFIG_REED_SOLOMON_ENC8
int pgm_encode_rs8 (struct rs_control*, guint8*, int, guint16*, guint16);
#endif
#ifdef CONFIG_REED_SOLOMON_DEC8
int pgm_decode_rs8 (struct rs_control*, guint8*, guint16*, int, guint16*, int, int*, guint16, guint16*);
#endif

/* General purpose RS codec, 16-bit data width, symbol width 1-15 bit  */
#ifdef CONFIG_REED_SOLOMON_ENC16
int pgm_encode_rs16 (struct rs_control*, guint16*, int, guint16*, guint16);
#endif
#ifdef CONFIG_REED_SOLOMON_DEC16
int pgm_decode_rs16 (struct rs_control*, guint16*, guint16*, int, guint16*, int, int*, guint16, guint16*);
#endif

/* Create or get a matching rs control structure */
struct rs_control *pgm_init_rs (int, int, int, int, int);

/* Release a rs control structure */
void pgm_free_rs(struct rs_control*);

/** modulo replacement for galois field arithmetics
 *
 *  @rs:	the rs control structure
 *  @x:		the value to reduce
 *
 *  where
 *  rs->mm = number of bits per symbol
 *  rs->nn = (2^rs->mm) - 1
 *
 *  Simple arithmetic modulo would return a wrong result for values
 *  >= 3 * rs->nn
*/
static inline int rs_modnn(struct rs_control *rs, int x)
{
	while (x >= rs->nn) {
		x -= rs->nn;
		x = (x >> rs->mm) + (x & rs->nn);
	}
	return x;
}

#endif /* __PGM_REED_SOLOMON_H__ */

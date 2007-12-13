/*
 * lib/reed_solomon/reed_solomon.c
 *
 * Overview:
 *   Generic Reed Solomon encoder / decoder library
 *
 * Copyright (C) 2004 Thomas Gleixner (tglx@linutronix.de)
 *
 * Reed Solomon code lifted from reed solomon library written by Phil Karn
 * Copyright 2002 Phil Karn, KA9Q
 *
 * $Id: rslib.c,v 1.7 2005/11/07 11:14:59 gleixner Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Description:
 *
 * The generic Reed Solomon library provides runtime configurable
 * encoding / decoding of RS codes.
 * Each user must call init_rs to get a pointer to a rs_control
 * structure for the given rs parameters. This structure is either
 * generated or a already available matching control structure is used.
 * If a structure is generated then the polynomial arrays for
 * fast encoding / decoding are built. This can take some time so
 * make sure not to call this function from a time critical path.
 * Usually a module / driver should initialize the necessary
 * rs_control structure on module / driver init and release it
 * on exit.
 * The encoding puts the calculated syndrome into a given syndrome
 * buffer.
 * The decoding is a two step process. The first step calculates
 * the syndrome over the received (data + syndrome) and calls the
 * second stage, which does the decoding / error correction itself.
 * Many hw encoders provide a syndrome calculation over the received
 * data + syndrome and can call the second stage directly.
 *
 */

#include <errno.h>
#include <string.h>

#include <glib.h>

#include "pgm/reed_solomon.h"


/* This list holds all currently allocated rs control structures */
static GList* g_rslist = NULL;

/* Protection for the list */
static GStaticMutex g_rslistlock;

/**
 * rs_init - Initialize a Reed-Solomon codec
 * @symsize:	symbol size, bits (1-8)
 * @gfpoly:	Field generator polynomial coefficients
 * @fcr:	first root of RS code generator polynomial, index form
 * @prim:	primitive element to generate polynomial roots
 * @nroots:	RS code generator polynomial degree (number of roots)
 *
 * Allocate a control structure and the polynom arrays for faster
 * en/decoding. Fill the arrays according to the given parameters.
 */
static struct rs_control*
rs_init (
	int	symsize,
	int	gfpoly,
	int	fcr,
	int	prim,
	int	nroots
	)
{
	struct rs_control *rs;
	int i, j, sr, root, iprim;

/* Allocate the control structure */
	rs = g_malloc0 (sizeof(struct rs_control));

	rs->mm = symsize;
	rs->nn = (1 << symsize) - 1;
	rs->fcr = fcr;
	rs->prim = prim;
	rs->nroots = nroots;
	rs->gfpoly = gfpoly;

/* Allocate the arrays */
	rs->alpha_to = g_malloc(sizeof(guint16) * (rs->nn + 1));
	rs->index_of = g_malloc(sizeof(guint16) * (rs->nn + 1));
	rs->genpoly = g_malloc(sizeof(guint16) * (rs->nroots + 1));

/* Generate Galois field lookup tables */
	rs->index_of[0] = rs->nn;	/* log(zero) = -inf */
	rs->alpha_to[rs->nn] = 0;	/* alpha**-inf = 0 */
	sr = 1;
	for (i = 0; i < rs->nn; i++) {
		rs->index_of[sr] = i;
		rs->alpha_to[i] = sr;
		sr <<= 1;
		if (sr & (1 << symsize))
			sr ^= gfpoly;
		sr &= rs->nn;
	}
/* If it's not primitive, exit */
	if(sr != 1)
		goto errpol;

/* Find prim-th root of 1, used in decoding */
	for(iprim = 1; (iprim % prim) != 0; iprim += rs->nn);
/* prim-th root of 1, index form */
	rs->iprim = iprim / prim;

/* Form RS code generator polynomial from its roots */
	rs->genpoly[0] = 1;
	for (i = 0, root = fcr * prim; i < nroots; i++, root += prim) {
		rs->genpoly[i + 1] = 1;
/* Multiply rs->genpoly[] by  @**(root + x) */
		for (j = i; j > 0; j--) {
			if (rs->genpoly[j] != 0) {
				rs->genpoly[j] = rs->genpoly[j -1] ^
					rs->alpha_to[rs_modnn(rs,
					rs->index_of[rs->genpoly[j]] + root)];
			} else
				rs->genpoly[j] = rs->genpoly[j - 1];
		}
/* rs->genpoly[0] can never be zero */
		rs->genpoly[0] =
			rs->alpha_to[rs_modnn(rs,
				rs->index_of[rs->genpoly[0]] + root)];
	}
/* convert rs->genpoly[] to index form for quicker encoding */
	for (i = 0; i <= nroots; i++)
		rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
	return rs;

/* Error exit */
errpol:
	g_free (rs->genpoly);
erridx:
	g_free (rs->index_of);
erralp:
	g_free (rs->alpha_to);
errrs:
	g_free (rs);
	return NULL;
}


/**
 *  free_rs - Free the rs control structure, if it is no longer used
 *  @rs:	the control structure which is not longer used by the
 *		caller
 */
void
pgm_free_rs (
	struct rs_control *rs
	)
{
	g_static_mutex_lock (&g_rslistlock);
	rs->users--;
	if(!rs->users) {
		g_list_delete_link (g_rslist, rs->link);
		g_free (rs->alpha_to);
		g_free (rs->index_of);
		g_free (rs->genpoly);
		g_free (rs);
	}
	g_static_mutex_unlock (&g_rslistlock);
}

/**
 * init_rs - Find a matching or allocate a new rs control structure
 *  @symsize:	the symbol size (number of bits)
 *  @gfpoly:	the extended Galois field generator polynomial coefficients,
 *		with the 0th coefficient in the low order bit. The polynomial
 *		must be primitive;
 *  @fcr:  	the first consecutive root of the rs code generator polynomial
 *		in index form
 *  @prim:	primitive element to generate polynomial roots
 *  @nroots:	RS code generator polynomial degree (number of roots)
 */
struct rs_control*
pgm_init_rs (
	int	symsize,
	int	gfpoly,
	int	fcr,
	int	prim,
	int	nroots
	)
{
	GList* tmp;
	struct rs_control	*rs;

	/* Sanity checks */
	if (symsize < 1)
		return NULL;
	if (fcr < 0 || fcr >= (1<<symsize))
    		return NULL;
	if (prim <= 0 || prim >= (1<<symsize))
    		return NULL;
	if (nroots < 0 || nroots >= (1<<symsize))
		return NULL;

	g_static_mutex_lock (&g_rslistlock);

	/* Walk through the list and look for a matching entry */
	tmp = g_rslist;
	while (tmp) {
		rs = (struct rs_control*)tmp->data;
		if (symsize != rs->mm)
			continue;
		if (gfpoly != rs->gfpoly)
			continue;
		if (fcr != rs->fcr)
			continue;
		if (prim != rs->prim)
			continue;
		if (nroots != rs->nroots)
			continue;
		/* We have a matching one already */
		rs->users++;
		goto out;

		tmp = tmp->next;
	}

	/* Create a new one */
	rs = rs_init (symsize, gfpoly, fcr, prim, nroots);
	if (rs) {
		rs->users = 1;
		rs->link = g_list_prepend (g_rslist, rs);
	}
out:
	g_static_mutex_unlock (&g_rslistlock);
	return rs;
}

#ifdef CONFIG_REED_SOLOMON_ENC8
/**
 *  encode_rs8 - Calculate the parity for data values (8bit data width)
 *  @rs:	the rs control structure
 *  @data:	data field of a given type
 *  @len:	data length
 *  @par:	parity data, must be initialized by caller (usually all 0)
 *  @invmsk:	invert data mask (will be xored on data)
 *
 *  The parity uses a guint16 data type to enable
 *  symbol size > 8. The calling code must take care of encoding of the
 *  syndrome result for storage itself.
 */
int
pgm_encode_rs8 (
	struct rs_control*	rs,
	guint8*			data,
	int			len,
	guint16*		par,
	guint16			invmsk
	)
{
#include "encode_rs.c"
}
#endif

#ifdef CONFIG_REED_SOLOMON_DEC8
/**
 *  decode_rs8 - Decode codeword (8bit data width)
 *  @rs:	the rs control structure
 *  @data:	data field of a given type
 *  @par:	received parity data field
 *  @len:	data length
 *  @s:		syndrome data field (if NULL, syndrome is calculated)
 *  @no_eras:	number of erasures
 *  @eras_pos:	position of erasures, can be NULL
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *  @corr:	buffer to store correction bitmask on eras_pos
 *
 *  The syndrome and parity uses a guint16 data type to enable
 *  symbol size > 8. The calling code must take care of decoding of the
 *  syndrome result and the received parity before calling this code.
 */
int
pgm_decode_rs8 (
	struct rs_control*	rs,
	guint8*			data,
	guint16*		par,
	int			len,
	guint16*		s,
	int			no_eras,
	int*			eras_pos,
	guint16			invmsk,
	guint16*		corr
	)
{
#include "decode_rs.c"
}
#endif

#ifdef CONFIG_REED_SOLOMON_ENC16
/**
 *  encode_rs16 - Calculate the parity for data values (16bit data width)
 *  @rs:	the rs control structure
 *  @data:	data field of a given type
 *  @len:	data length
 *  @par:	parity data, must be initialized by caller (usually all 0)
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *
 *  Each field in the data array contains up to symbol size bits of valid data.
 */
int
pgm_encode_rs16 (
	struct rs_control*	rs,
	guint16*		data,
	int			len,
	guint16*		par,
	guint16			invmsk
	)
{
#include "encode_rs.c"
}
#endif

#ifdef CONFIG_REED_SOLOMON_DEC16
/**
 *  decode_rs16 - Decode codeword (16bit data width)
 *  @rs:	the rs control structure
 *  @data:	data field of a given type
 *  @par:	received parity data field
 *  @len:	data length
 *  @s:		syndrome data field (if NULL, syndrome is calculated)
 *  @no_eras:	number of erasures
 *  @eras_pos:	position of erasures, can be NULL
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *  @corr:	buffer to store correction bitmask on eras_pos
 *
 *  Each field in the data array contains up to symbol size bits of valid data.
 */
int
pgm_decode_rs16 (
	struct rs_control*	rs,
	guint16*		data,
	guint16*		par,
	int			len,
	guint16*		s,
	int			no_eras,
	int*			eras_pos,
	guint16			invmsk,
	guint16*		corr
	)
{
#include "decode_rs.c"
}
#endif



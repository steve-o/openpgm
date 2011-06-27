/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Reed-Solomon forward error correction based on Vandermonde matrices.
 *
 * Output is incompatible with BCH style Reed-Solomon encoding.
 *
 * draft-ietf-rmt-bb-fec-rs-05.txt
 * + rfc5052
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/framework.h>


/* Vector GF(2⁸) plus-equals multiplication.
 *
 * d[] += b • s[]
 */

static
void
_pgm_gf_vec_addmul (
	pgm_gf8_t*	 restrict d,
	const pgm_gf8_t		  b,
	const pgm_gf8_t* restrict s,
	uint16_t		  len	/* length of vectors */
	)
{
	uint_fast16_t i;
	uint_fast16_t count8;

	if (PGM_UNLIKELY(b == 0))
		return;

#ifdef USE_GALOIS_MUL_LUT
        const pgm_gf8_t* gfmul_b = &pgm_gftable[ (uint16_t)b << 8 ];
#endif

	i = 0;
	count8 = len >> 3;		/* 8-way unrolls */
	if (count8)
	{
		while (count8--) {
#ifdef USE_GALOIS_MUL_LUT
			d[i  ] ^= gfmul_b[ s[i  ] ];
			d[i+1] ^= gfmul_b[ s[i+1] ];
			d[i+2] ^= gfmul_b[ s[i+2] ];
			d[i+3] ^= gfmul_b[ s[i+3] ];
			d[i+4] ^= gfmul_b[ s[i+4] ];
			d[i+5] ^= gfmul_b[ s[i+5] ];
			d[i+6] ^= gfmul_b[ s[i+6] ];
			d[i+7] ^= gfmul_b[ s[i+7] ];
#else
			d[i  ] ^= gfmul( b, s[i  ] );
			d[i+1] ^= gfmul( b, s[i+1] );
			d[i+2] ^= gfmul( b, s[i+2] );
			d[i+3] ^= gfmul( b, s[i+3] );
			d[i+4] ^= gfmul( b, s[i+4] );
			d[i+5] ^= gfmul( b, s[i+5] );
			d[i+6] ^= gfmul( b, s[i+6] );
			d[i+7] ^= gfmul( b, s[i+7] );
#endif
			i += 8;
		}

/* remaining */
		len %= 8;
	}

	while (len--) {
#ifdef USE_GALOIS_MUL_LUT
		d[i] ^= gfmul_b[ s[i] ];
#else
		d[i] ^= gfmul( b, s[i] );
#endif
		i++;
	}
}

/* Basic matrix multiplication.
 *
 * C = AB
 *         n
 * c_i,j = ∑  a_i,j × b_r,j = a_i,1 × b_1,j + a_i,2 × b_2,j + ⋯ + a_i,n × b_n,j
 *        r=1
 */

static
void
_pgm_matmul (
	const pgm_gf8_t* restrict a,	/* m-by-n */
	const pgm_gf8_t* restrict b,	/* n-by-p */
	pgm_gf8_t*	 restrict c,	/* ∴ m-by-p */
	const uint16_t		  m,
	const uint16_t		  n,
	const uint16_t		  p
	)
{
	for (uint_fast16_t j = 0; j < m; j++)
	{
		for (uint_fast16_t i = 0; i < p; i++)
		{
			pgm_gf8_t sum = 0;

			for (uint_fast16_t k = 0; k < n; k++)
			{
				sum ^= pgm_gfmul ( a[ (j * n) + k ], b[ (k * p) + i ] );
			}

			c[ (j * p) + i ] = sum;
		}
	}
}

/* Generic square matrix inversion
 */

#ifdef USE_XOR_SWAP
/* whilst cute the xor swap is quite slow */
#define SWAP(a, b)	(((a) ^= (b)), ((b) ^= (a)), ((a) ^= (b)))
#else
#define SWAP(a, b)	do { const pgm_gf8_t _t = (b); (b) = (a); (a) = _t; } while (0)
#endif

static
void
_pgm_matinv (
	pgm_gf8_t*		M,		/* is n-by-n */
	const uint8_t		n
	)
{
	uint8_t pivot_rows[ n ];
	uint8_t pivot_cols[ n ];
	bool pivots[ n ];
	memset (pivots, 0, sizeof(pivots));

	pgm_gf8_t identity[ n ];
	memset (identity, 0, sizeof(identity));

	for (uint_fast8_t i = 0; i < n; i++)
	{
		uint_fast8_t row = 0, col = 0;

/* check diagonal for new pivot */
		if (!pivots[ i ] && M[ (i * n) + i ])
		{
			row = col = i;
		}
		else
		{
			for (uint_fast8_t j = 0; j < n; j++)
			{
				if (pivots[ j ]) continue;

				for (uint_fast8_t x = 0; x < n; x++)
				{
					if (!pivots[ x ] && M[ (j * n) + x ])
					{
						row = j;
						col = x;
						goto found;
					}
				}
			}
		}

found:
		pivots[ col ] = TRUE;

/* pivot */
		if (row != col)
		{
			for (uint_fast8_t x = 0; x < n; x++)
			{
				pgm_gf8_t *pivot_row = &M[ (row * n) + x ],
				          *pivot_col = &M[ (col * n) + x ];
				SWAP( *pivot_row, *pivot_col );
			}
		}

/* save location */
		pivot_rows[ i ] = row;
		pivot_cols[ i ] = col;

/* divide row by pivot element */
		if (M[ (col * n) + col ] != 1)
		{
			const pgm_gf8_t c = M[ (col * n) + col ];
			                    M[ (col * n) + col ] = 1;

			for (uint_fast8_t x = 0; x < n; x++)
			{
				M[ (col * n) + x ] = pgm_gfdiv( M[ (col * n) + x ], c );
			}
		}

/* reduce if not an identity row */
		identity[ col ] = 1;
		if (memcmp (&M[ (col * n) ], identity, n * sizeof(pgm_gf8_t)))
		{
			for (	uint_fast8_t x = 0;
				x < n;
				x++ )
			{
				if (x == col) continue;

				const pgm_gf8_t c = M[ (x * n) + col ];
				                    M[ (x * n) + col ] = 0;

				_pgm_gf_vec_addmul (&M[ x * n ], c, &M[ col * n ], n);
			}
		}
		identity[ col ] = 0;
	}

/* revert all pivots */
	for (int_fast16_t i = n - 1; i >= 0; i--)
	{
		if (pivot_rows[ i ] != pivot_cols[ i ])
		{
			for (uint_fast8_t j = 0; j < n; j++)
			{
				pgm_gf8_t *pivot_row = &M[ (j * n) + pivot_rows[ i ] ],
				          *pivot_col = &M[ (j * n) + pivot_cols[ i ] ];
				SWAP( *pivot_row, *pivot_col );
			}
		}
	}
}

/* Gauss–Jordan elimination optimised for Vandermonde matrices
 *
 * matrix = matrix⁻¹
 *
 * A Vandermonde matrix exhibits geometric progression in each row:
 *
 *     ⎡  1  α₁  α₁² ⋯ α₁^^(n-1) ⎤
 * V = ⎢  1  α₂  α₂² ⋯ α₂^^(n-1) ⎥
 *     ⎣  1  α₃  α₃² ⋯ α₃^^(n-1) ⎦
 *
 * First column is actually α_m⁰, second column is α_m¹.
 *
 * nb: produces a modified Vandermonde matrix optimised for subsequent
 * multiplication terms.
 */

static
void
_pgm_matinv_vandermonde (
	pgm_gf8_t*		V,		/* is n-by-n */
	const uint8_t		n
	)
{
/* trivial cases */
	if (n == 1) return;

/* P_j(α) is polynomial of degree n - 1 defined by
 *
 *          n 
 * P_j(α) = ∏ (α - α_m)
 *         m=1
 *
 * 1: Work out coefficients.
 */

	pgm_gf8_t P[ n ];
	memset (P, 0, sizeof(P));

/* copy across second row, i.e. j = 2 */
	for (uint_fast8_t i = 0; i < n; i++)
	{
		P[ i ] = V[ (i * n) + 1 ];
	}

	pgm_gf8_t alpha[ n ];
	memset (alpha, 0, sizeof(alpha));

	alpha[ n - 1 ] = P[ 0 ];
	for (uint_fast8_t i = 1; i < n; i++)
	{
		for (uint_fast8_t j = (n - i); j < (n - 1); j++)
		{
			alpha[ j ] ^= pgm_gfmul( P[ i ], alpha[ j + 1 ] );
		}
		alpha[ n - 1 ] ^= P[ i ];
	}

/* 2: Obtain numberators and denominators by synthetic division.
 */

	pgm_gf8_t b[ n ];
	b[ n - 1 ] = 1;
	for (uint_fast8_t j = 0; j < n; j++)
	{
		const pgm_gf8_t xx = P[ j ];
		pgm_gf8_t t = 1;

/* skip first iteration */
		for (int_fast16_t i = n - 2; i >= 0; i--)
		{
			b[ i ] = alpha[ i + 1 ] ^ pgm_gfmul( xx, b[ i + 1 ] );
			t = pgm_gfmul( xx, t ) ^ b[ i ];
		}

		for (uint_fast8_t i = 0; i < n; i++)
		{
			V[ (i * n) + j ] = pgm_gfdiv ( b[ i ], t );
		}
	}
}

/* create the generator matrix of a reed-solomon code.
 *
 *          s             GM            e
 *   ⎧  ⎡   s₀  ⎤   ⎡ 1 0     0 ⎤   ⎡   e₀  ⎤  ⎫
 *   ⎪  ⎢   ⋮   ⎥   ⎢ 0 1       ⎥ = ⎢   ⋮   ⎥  ⎬ n
 * k ⎨  ⎢   ⋮   ⎥ × ⎢     ⋱     ⎥   ⎣e_{n-1}⎦  ⎭
 *   ⎪  ⎢   ⋮   ⎥   ⎢       ⋱   ⎥
 *   ⎩  ⎣s_{k-1}⎦   ⎣ 0 0     1 ⎦
 *
 * e = s × GM
 */

PGM_GNUC_INTERNAL
void
pgm_rs_create (
	pgm_rs_t*		rs,
	const uint8_t		n,
	const uint8_t		k
	)
{
	pgm_assert (NULL != rs);
	pgm_assert (n > 0);
	pgm_assert (k > 0);

	rs->n	= n;
	rs->k	= k;
	rs->GM	= pgm_new0 (pgm_gf8_t, n * k);
	rs->RM	= pgm_new0 (pgm_gf8_t, k * k);

/* alpha = root of primitive polynomial of degree m
 *                 ( 1 + x² + x³ + x⁴ + x⁸ )
 *
 * V = Vandermonde matrix of k rows and n columns.
 *
 * Be careful, Harry!
 */
#ifdef USE_MALLOC_MATRIX
	pgm_gf8_t* V = pgm_new0 (pgm_gf8_t, n * k);
#else
	pgm_gf8_t* V = pgm_newa (pgm_gf8_t, n * k);
	memset (V, 0, n * k);
#endif
	pgm_gf8_t* p = V + k;
	V[0] = 1;
	for (uint_fast8_t j = 0; j < (n - 1); j++)
	{
		for (uint_fast8_t i = 0; i < k; i++)
		{
/* the {i, j} entry of V_{k,n} is v_{i,j} = α^^(i×j),
 * where 0 <= i <= k - 1 and 0 <= j <= n - 1.
 */
			*p++ = pgm_gfantilog[ ( i * j ) % PGM_GF_MAX ];
		}
	}

/* This generator matrix would create a Maximum Distance Separable (MDS)
 * matrix, a systematic result is required, i.e. original data is left
 * unchanged.
 *
 * GM = V_{k,k}⁻¹ × V_{k,n}
 *
 * 1: matrix V_{k,k} formed by the first k columns of V_{k,n}
 */
	pgm_gf8_t* V_kk = V;
	pgm_gf8_t* V_kn = V + (k * k);

/* 2: invert it
 */
	_pgm_matinv_vandermonde (V_kk, k);

/* 3: multiply by V_{k,n}
 */
	_pgm_matmul (V_kn, V_kk, rs->GM + (k * k), n - k, k, k);

#ifdef USE_MALLOC_MATRIX
	pgm_free (V);
#endif

/* 4: set identity matrix for original data
 */
	for (uint_fast8_t i = 0; i < k; i++)
	{
		rs->GM[ (i * k) + i ] = 1;
	}
}

PGM_GNUC_INTERNAL
void
pgm_rs_destroy (
	pgm_rs_t*		rs
	)
{
	pgm_assert (NULL != rs);

	if (rs->RM) {
		pgm_free (rs->RM);
		rs->RM = NULL;
	}

	if (rs->GM) {
		pgm_free (rs->GM);
		rs->GM = NULL;
	}
}

/* create a parity packet from a vector of original data packets and
 * FEC block packet offset.
 */

PGM_GNUC_INTERNAL
void
pgm_rs_encode (
	pgm_rs_t*	  restrict rs,
	const pgm_gf8_t** restrict src,		/* length rs_t::k */
	const uint8_t		   offset,
	pgm_gf8_t*	  restrict dst,
	const uint16_t		   len
	)
{
	pgm_assert (NULL != rs);
	pgm_assert (NULL != src);
	pgm_assert (offset >= rs->k && offset < rs->n);	/* parity packet */
	pgm_assert (NULL != dst);
	pgm_assert (len > 0);

	memset (dst, 0, len);
	for (uint_fast8_t i = 0; i < rs->k; i++)
	{
		const pgm_gf8_t c = rs->GM[ (offset * rs->k) + i ];
		_pgm_gf_vec_addmul (dst, c, src[i], len);
	}
}

/* original data block of packets with missing packet entries replaced
 * with on-demand parity packets.
 */

PGM_GNUC_INTERNAL
void
pgm_rs_decode_parity_inline (
	pgm_rs_t*      restrict rs,
	pgm_gf8_t**    restrict block,		/* length rs_t::k */
	const uint8_t* restrict	offsets,	/* offsets within FEC block, 0 < offset < n */
	const uint16_t	        len		/* packet length */
	)
{
	pgm_assert (NULL != rs);
	pgm_assert (NULL != block);
	pgm_assert (NULL != offsets);
	pgm_assert (len > 0);

/* create new recovery matrix from generator
 */
	for (uint_fast8_t i = 0; i < rs->k; i++)
	{
		if (offsets[i] < rs->k) {
			memset (&rs->RM[ i * rs->k ], 0, rs->k * sizeof(pgm_gf8_t));
			rs->RM[ (i * rs->k) + i ] = 1;
			continue;
		}
		memcpy (&rs->RM[ i * rs->k ], &rs->GM[ offsets[ i ] * rs->k ], rs->k * sizeof(pgm_gf8_t));
	}

/* invert */
	_pgm_matinv (rs->RM, rs->k);

	pgm_gf8_t* repairs[ rs->k ];

/* multiply out, through the length of erasures[] */
	for (uint_fast8_t j = 0; j < rs->k; j++)
	{
		if (offsets[ j ] < rs->k)
			continue;

#ifdef USE_MALLOC_MATRIX
		pgm_gf8_t* erasure = repairs[ j ] = pgm_malloc0 (len);
#else
		pgm_gf8_t* erasure = repairs[ j ] = pgm_alloca (len);
		memset (erasure, 0, len);
#endif
		for (uint_fast8_t i = 0; i < rs->k; i++)
		{
			pgm_gf8_t* src = block[ i ];
			pgm_gf8_t c = rs->RM[ (j * rs->k) + i ];
			_pgm_gf_vec_addmul (erasure, c, src, len);
		}
	}

/* move repaired over parity packets */
	for (uint_fast8_t j = 0; j < rs->k; j++)
	{
		if (offsets[ j ] < rs->k)
			continue;

		memcpy (block[ j ], repairs[ j ], len * sizeof(pgm_gf8_t));
#ifdef USE_MALLOC_MATRIX
		pgm_free (repairs[ j ]);
#endif
	}
}

/* entire FEC block of original data and parity packets.
 *
 * erased packet buffers must be zeroed.
 */

PGM_GNUC_INTERNAL
void
pgm_rs_decode_parity_appended (
	pgm_rs_t*      restrict rs,
	pgm_gf8_t**    restrict block,	/* length rs_t::n, the FEC block */
	const uint8_t* restrict offsets,	/* ordered index of packets */
	const uint16_t	        len		/* packet length */
	)
{
	pgm_assert (NULL != rs);
	pgm_assert (NULL != block);
	pgm_assert (NULL != offsets);
	pgm_assert (len > 0);

/* create new recovery matrix from generator
 */
	for (uint_fast8_t i = 0; i < rs->k; i++)
	{
		if (offsets[i] < rs->k) {
			memset (&rs->RM[ i * rs->k ], 0, rs->k * sizeof(pgm_gf8_t));
			rs->RM[ (i * rs->k) + i ] = 1;
			continue;
		}
		memcpy (&rs->RM[ i * rs->k ], &rs->GM[ offsets[ i ] * rs->k ], rs->k * sizeof(pgm_gf8_t));
	}

/* invert */
	_pgm_matinv (rs->RM, rs->k);

/* multiply out, through the length of erasures[] */
	for (uint_fast8_t j = 0; j < rs->k; j++)
	{
		if (offsets[ j ] < rs->k)
			continue;

		uint_fast8_t p = rs->k;
		pgm_gf8_t* erasure = block[ j ];
		for (uint_fast8_t i = 0; i < rs->k; i++)
		{
			pgm_gf8_t* src;
			if (offsets[ i ] < rs->k)
				src = block[ i ];
			else
				src = block[ p++ ];
			const pgm_gf8_t c = rs->RM[ (j * rs->k) + i ];
			_pgm_gf_vec_addmul (erasure, c, src, len);
		}
	}
}

/* eof */

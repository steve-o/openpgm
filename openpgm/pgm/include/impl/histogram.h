/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * histograms.
 *
 * Copyright (c) 2009-2010 Miru Limited.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_HISTOGRAM_H__
#define __PGM_IMPL_HISTOGRAM_H__

#include <pgm/types.h>
#include <pgm/time.h>
#include <impl/slist.h>
#include <impl/string.h>

PGM_BEGIN_DECLS

typedef int pgm_sample_t;
typedef int pgm_count_t;

struct pgm_sample_set_t {
	pgm_count_t*	counts;
	unsigned	counts_len;
	int64_t		sum;
	int64_t		square_sum;
};

typedef struct pgm_sample_set_t pgm_sample_set_t;

struct pgm_histogram_t {
	const char* restrict	histogram_name;
	unsigned		bucket_count;
	pgm_sample_t		declared_min;
	pgm_sample_t		declared_max;
	pgm_sample_t* restrict	ranges;
	pgm_sample_set_t	sample;
	bool			is_registered;
	pgm_slist_t		histograms_link;
};

typedef struct pgm_histogram_t pgm_histogram_t;

#define PGM_HISTOGRAM_DEFINE(name, minimum, maximum, count) \
		static pgm_count_t counts[ (count) ]; \
		static pgm_sample_t ranges[ (count) + 1 ]; \
		static pgm_histogram_t counter = { \
			.histogram_name		= (name), \
			.bucket_count		= (count), \
			.declared_min		= (minimum), \
			.declared_max		= (maximum), \
			.ranges			= ranges, \
			.sample = { \
				.counts 		= counts, \
				.counts_len		= (count), \
				.sum			= 0, \
				.square_sum		= 0 \
			}, \
			.is_registered		= FALSE \
		}

#ifdef USE_HISTOGRAMS

#	define PGM_HISTOGRAM_TIMES(name, sample) do { \
		PGM_HISTOGRAM_DEFINE(name, pgm_msecs(1), pgm_secs(10), 50); \
		if (!counter.is_registered) { \
			memset (counts, 0, sizeof(counts)); \
			memset (ranges, 0, sizeof(ranges)); \
			pgm_histogram_init (&counter); \
		} \
		pgm_histogram_add_time (&counter, sample); \
	} while (0)

#	define PGM_HISTOGRAM_COUNTS(name, sample) do { \
		PGM_HISTOGRAM_DEFINE(name, 1, 1000000, 50); \
		if (!counter.is_registered) { \
			memset (counts, 0, sizeof(counts)); \
			memset (ranges, 0, sizeof(ranges)); \
			pgm_histogram_init (&counter); \
		} \
		pgm_histogram_add (&counter, (sample)); \
	} while (0)

#else

#	define PGM_HISTOGRAM_TIMES(name, sample)
#	define PGM_HISTOGRAM_COUNTS(name, sample)

#endif /* USE_HISTOGRAMS */


extern pgm_slist_t*	pgm_histograms;

void pgm_histogram_init (pgm_histogram_t*);
void pgm_histogram_add (pgm_histogram_t*, int);
void pgm_histogram_write_html_graph_all (pgm_string_t*);

static inline
void
pgm_histogram_add_time (
	pgm_histogram_t*const	histogram,
	pgm_time_t		sample_time
	)
{
	pgm_histogram_add (histogram, (int)pgm_to_msecs (sample_time));
}

PGM_END_DECLS

#endif /* __PGM_IMPL_HISTOGRAM_H__ */

/* eof */

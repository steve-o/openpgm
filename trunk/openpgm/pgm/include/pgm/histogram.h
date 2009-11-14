/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * histograms.
 *
 * Copyright (c) 2009 Miru Limited.
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

#ifndef __PGM_HISTOGRAM_H__
#define __PGM_HISTOGRAM_H__

#include <glib.h>

#ifndef __PGM_TIME_H__
#	include <pgm/time.h>
#endif


typedef int pgm_sample_t;
typedef int pgm_count_t;

struct pgm_sample_set_t {
	pgm_count_t*	counts;
	gint		counts_len;
	gint64		sum;
	gint64		square_sum;
};

typedef struct pgm_sample_set_t pgm_sample_set_t;

struct pgm_histogram_t {
	const gchar*		histogram_name;
	gsize			bucket_count;
	pgm_sample_t		declared_min;
	pgm_sample_t		declared_max;
	pgm_sample_t*		ranges;
	pgm_sample_set_t	sample;
	gboolean		is_registered;
	GSList			histograms_link;
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

#ifdef CONFIG_HISTOGRAMS

#define PGM_HISTOGRAM_TIMES(name, sample) do { \
		PGM_HISTOGRAM_DEFINE(name, pgm_msecs(1), pgm_secs(10), 50); \
		if (!counter.is_registered) { \
			memset (counts, 0, sizeof(counts)); \
			memset (ranges, 0, sizeof(ranges)); \
			pgm_histogram_init (&counter); \
		} \
		pgm_histogram_add_time (&counter, sample); \
	} while (0)

#define PGM_HISTOGRAM_COUNTS(name, sample) do { \
		PGM_HISTOGRAM_DEFINE(name, 1, 1000000, 50); \
		if (!counter.is_registered) { \
			memset (counts, 0, sizeof(counts)); \
			memset (ranges, 0, sizeof(ranges)); \
			pgm_histogram_init (&counter); \
		} \
		pgm_histogram_add (&counter, (sample)); \
	} while (0)

#else /* !CONFIG_HISTOGRAMS */

#define PGM_HISTOGRAM_TIMES(name, sample)
#define PGM_HISTOGRAM_COUNTS(name, sample)

#endif /* !CONFIG_HISTOGRAMS */


extern GSList* pgm_histograms;

G_BEGIN_DECLS

void pgm_histogram_init (pgm_histogram_t*);
void pgm_histogram_add (pgm_histogram_t*, int);
static inline void pgm_histogram_add_time (pgm_histogram_t* histogram, pgm_time_t sample_time) {
	pgm_histogram_add (histogram, (int)pgm_to_msecs (sample_time));
}

void pgm_histogram_write_html_graph_all (GString*);

G_END_DECLS

#endif /* __PGM_HISTOGRAM_H__ */

/* eof */

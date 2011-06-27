/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Histograms.
 *
 * Copyright (c) 2009-2011 Miru Limited.
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
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <impl/framework.h>


//#define HISTOGRAM_DEBUG


pgm_slist_t* pgm_histograms = NULL;


static void sample_set_accumulate (pgm_sample_set_t*, pgm_sample_t, pgm_count_t, unsigned);
static pgm_count_t sample_set_total_count (const pgm_sample_set_t*) PGM_GNUC_PURE;

static void set_bucket_range (pgm_histogram_t*, unsigned, pgm_sample_t);
static void initialize_bucket_range (pgm_histogram_t*);
static unsigned bucket_index (const pgm_histogram_t*, const pgm_sample_t);
static void accumulate (pgm_histogram_t*, pgm_sample_t, pgm_count_t, unsigned);
static double get_peak_bucket_size (const pgm_histogram_t*restrict, const pgm_sample_set_t*restrict);
static double get_bucket_size (const pgm_histogram_t*, const pgm_count_t, const unsigned);

static void pgm_histogram_write_html_graph (pgm_histogram_t*restrict, pgm_string_t*restrict);
static void write_ascii (pgm_histogram_t*restrict, const char*restrict, pgm_string_t*restrict);
static void write_ascii_header (pgm_histogram_t*restrict, pgm_sample_set_t*restrict, pgm_count_t, pgm_string_t*restrict);
static void write_ascii_bucket_graph (double, double, pgm_string_t*);
static void write_ascii_bucket_context (int64_t, pgm_count_t, int64_t, unsigned, pgm_string_t*);
static void write_ascii_bucket_value (pgm_count_t, double, pgm_string_t*);
static pgm_string_t* get_ascii_bucket_range (pgm_histogram_t*, unsigned);


void
pgm_histogram_add (
	pgm_histogram_t*	histogram,
	int			value
	)
{
	if (value > INT_MAX)
		value = INT_MAX - 1;
	if (value < 0)
		value = 0;
	const unsigned i = bucket_index (histogram, value);
	pgm_assert_cmpint (value, >=, histogram->ranges[ i ]);
	pgm_assert_cmpint (value,  <, histogram->ranges[ i + 1 ]);
	accumulate (histogram, value, 1, i);
}

void
pgm_histogram_write_html_graph_all (
	pgm_string_t*		string
	)
{
	if (!pgm_histograms)
		return;
	pgm_slist_t* snapshot = pgm_histograms;
	while (snapshot) {
		pgm_histogram_t* histogram = snapshot->data;
		pgm_histogram_write_html_graph (histogram, string);
		snapshot = snapshot->next;
	}
}

static
void
pgm_histogram_write_html_graph (
	pgm_histogram_t* restrict histogram,
	pgm_string_t*	 restrict string
	)
{
	pgm_string_append (string, "<PRE>");
	write_ascii (histogram, "<BR/>", string);
	pgm_string_append (string, "</PRE>");
}

static
void
sample_set_accumulate (
	pgm_sample_set_t*	sample_set,
	pgm_sample_t		value,
	pgm_count_t		count,
	unsigned		i
	)
{
	pgm_assert (1 == count || -1 == count);
	sample_set->counts[ i ] += count;
	sample_set->sum += count * value;
	sample_set->square_sum += (count * value) * (int64_t)value;
	pgm_assert_cmpint (sample_set->counts[ i ], >=, 0);
	pgm_assert_cmpint (sample_set->sum, >=, 0);
	pgm_assert_cmpint (sample_set->square_sum, >=, 0);
}

static
pgm_count_t
sample_set_total_count (
	const pgm_sample_set_t*	sample_set
	)
{
	pgm_count_t total = 0;
	for (unsigned i = 0; i < sample_set->counts_len; i++)
		total += sample_set->counts[ i ];
	return total;
}

void
pgm_histogram_init (
	pgm_histogram_t*	histogram
	)
{
	if (histogram->declared_min <= 0)
		histogram->declared_min = 1;
	pgm_assert_cmpint (histogram->declared_min, >, 0);
	histogram->declared_max = INT_MAX - 1;
	pgm_assert_cmpint (histogram->declared_min, <=, histogram->declared_max);
	pgm_assert_cmpuint (1, <, histogram->bucket_count);
	set_bucket_range (histogram, histogram->bucket_count, INT_MAX);
	initialize_bucket_range (histogram);

/* register with global list */
	histogram->histograms_link.data = histogram;
	histogram->histograms_link.next = pgm_histograms;
	pgm_histograms = &histogram->histograms_link;
	histogram->is_registered = TRUE;
}

static
void
set_bucket_range (
	pgm_histogram_t*	histogram,
	unsigned		i,
	pgm_sample_t		value
	)
{
	histogram->ranges[ i ] = value;
}

static
void
initialize_bucket_range (
	pgm_histogram_t*	histogram
	)
{
	const double log_max = log ((double)histogram->declared_max);
	double log_ratio;
	double log_next;
	unsigned i = 1;
	pgm_sample_t current = histogram->declared_min;

	set_bucket_range (histogram, i, current);
	while (histogram->bucket_count > ++i) {
		const double log_current = log ((double)current);
		log_ratio = (log_max - log_current) / (histogram->bucket_count - i);
		log_next = log_current + log_ratio;
#ifdef __GNUC__	
		const int next = floor (exp (log_next) + 0.5);
#else
/* bad-function-cast warning in GCC */
		const int next = (int)(floor (exp (log_next) + 0.5));
#endif
		if (next > current)
			current = next;
		else
			current++;
		set_bucket_range (histogram, i, current);
	}
	pgm_assert_cmpuint (histogram->bucket_count, ==, i);
}

static
unsigned
bucket_index (
	const pgm_histogram_t*	histogram,
	const pgm_sample_t	value
	)
{
	pgm_assert_cmpint (histogram->ranges[0], <=, value);
	pgm_assert_cmpint (histogram->ranges[ histogram->bucket_count ], >, value);
	unsigned under = 0;
	unsigned over = histogram->bucket_count;
	unsigned mid;

	do {
		pgm_assert_cmpuint (over, >=, under);
		mid = ((unsigned)under + (unsigned)over) >> 1;
		if (mid == under)
			break;
		if (histogram->ranges[ mid ] <= value)
			under = mid;
		else
			over = mid;
	} while (TRUE);
	pgm_assert (histogram->ranges[ mid ] <= value && histogram->ranges[ mid + 1] > value);
	return mid;
}

static
void
accumulate (
	pgm_histogram_t*	histogram,
	pgm_sample_t		value,
	pgm_count_t		count,
	unsigned		i
	)
{
	sample_set_accumulate (&histogram->sample, value, count, i);
}

static
void
write_ascii (
	pgm_histogram_t* restrict histogram,
	const char*	 restrict newline,
	pgm_string_t*	 restrict output
	)
{
	pgm_count_t snapshot_counts[ histogram->sample.counts_len ];
	pgm_sample_set_t snapshot = {
		.counts		= snapshot_counts,
		.counts_len	= histogram->sample.counts_len,
		.sum		= histogram->sample.sum,
		.square_sum	= histogram->sample.square_sum
	};
	memcpy (snapshot_counts, histogram->sample.counts, sizeof(pgm_count_t) * histogram->sample.counts_len);

	pgm_count_t sample_count = sample_set_total_count (&snapshot);
	write_ascii_header (histogram, &snapshot, sample_count, output);
	pgm_string_append (output, newline);

	double max_size = get_peak_bucket_size (histogram, &snapshot);
	unsigned largest_non_empty_bucket = histogram->bucket_count - 1;
	while (0 == snapshot.counts[ largest_non_empty_bucket ])
	{
		if (0 == largest_non_empty_bucket)
			break;
		largest_non_empty_bucket--;
	}

	int print_width = 1;
	for (unsigned i = 0; i < histogram->bucket_count; ++i)
	{
		if (snapshot.counts[ i ]) {
			pgm_string_t* bucket_range = get_ascii_bucket_range (histogram, i);
			const int width = (int)(bucket_range->len + 1);
			pgm_string_free (bucket_range, TRUE);
			if (width > print_width)
				print_width = width;
		}
	}

	int64_t remaining = sample_count;
	int64_t past = 0;
	for (unsigned i = 0; i < histogram->bucket_count; ++i)
	{
		pgm_count_t current = snapshot.counts[ i ];
		remaining -= current;
		pgm_string_t* bucket_range = get_ascii_bucket_range (histogram, i);
		pgm_string_append_printf (output, "%*s ", print_width, bucket_range->str);
		pgm_string_free (bucket_range, TRUE);
		if (0 == current &&
		    i < histogram->bucket_count - 1 &&
		    0 == snapshot.counts[ i + 1 ])
		{
			while (i < histogram->bucket_count - 1 &&
			       0 == snapshot.counts[ i + 1 ])
			{
				i++;
			}
			pgm_string_append (output, "... ");
			pgm_string_append (output, newline);
			continue;
		}

		const double current_size = get_bucket_size (histogram, current, i);
		write_ascii_bucket_graph (current_size, max_size, output);
		write_ascii_bucket_context (past, current, remaining, i, output);
		pgm_string_append (output, newline);
		past += current;
	}
}

static
void
write_ascii_header (
	pgm_histogram_t*  restrict histogram,
	pgm_sample_set_t* restrict sample_set,
	pgm_count_t		   sample_count,
	pgm_string_t*	  restrict output
	)
{
	pgm_string_append_printf (output,
				 "Histogram: %s recorded %d samples",
				 histogram->histogram_name ? histogram->histogram_name : "(null)",
				 sample_count);
	if (sample_count > 0) {
		const double average  = (float)(sample_set->sum) / sample_count;
		const double variance = (float)(sample_set->square_sum) / sample_count
						- average * average;
		const double standard_deviation = sqrt (variance);
		pgm_string_append_printf (output,
					 ", average = %.1f, standard deviation = %.1f",
					 average, standard_deviation);
	}
}

static
void
write_ascii_bucket_graph (
	double			current_size,
	double			max_size,
	pgm_string_t*		output
	)
{
	static const int k_line_length = 72;
	int x_count = (int)(k_line_length * (current_size / max_size) + 0.5);
	int x_remainder = k_line_length - x_count;
	while (0 < x_count--)
		pgm_string_append_c (output, '-');
	pgm_string_append_c (output, 'O');
	while (0 < x_remainder--)
		pgm_string_append_c (output, ' ');
}

static
void
write_ascii_bucket_context (
	int64_t			past,
	pgm_count_t		current,
	int64_t			remaining,
	unsigned		i,
	pgm_string_t*		output
	)
{
	const double scaled_sum = (past + current + remaining) / 100.0;
	write_ascii_bucket_value (current, scaled_sum, output);
	if (0 < i) {
		const double percentage = past / scaled_sum;
		pgm_string_append_printf (output, " {%3.1f%%}", percentage);
	}
}

static
void
write_ascii_bucket_value (
	pgm_count_t		current,
	double			scaled_sum,
	pgm_string_t*		output
	)
{
	pgm_string_append_printf (output, " (%d = %3.1f%%)", current, current/scaled_sum);
}

static
double
get_peak_bucket_size (
	const pgm_histogram_t*	restrict histogram,
	const pgm_sample_set_t* restrict sample_set
	)
{
	double max_size = 0;
	for (unsigned i = 0; i < histogram->bucket_count; i++) {
		const double current_size = get_bucket_size (histogram, sample_set->counts[ i ], i);
		if (current_size > max_size)
			max_size = current_size;
	}
	return max_size;
}

static
double
get_bucket_size (
	const pgm_histogram_t*	histogram,
	const pgm_count_t	current,
	const unsigned		i
	)
{
	pgm_assert_cmpint (histogram->ranges[ i + 1 ], >, histogram->ranges[ i ]);
	static const double kTransitionWidth = 5;
	double denominator = histogram->ranges[ i + 1 ] - histogram->ranges[ i ];
	if (denominator > kTransitionWidth)
		denominator = kTransitionWidth;
	return current / denominator;
}

static
pgm_string_t*
get_ascii_bucket_range (
	pgm_histogram_t*	histogram,
	unsigned		i
	)
{
	pgm_string_t* result = pgm_string_new (NULL);
	pgm_string_printf (result, "%d", histogram->ranges[ i ]);
	return result;
}

/* eof */

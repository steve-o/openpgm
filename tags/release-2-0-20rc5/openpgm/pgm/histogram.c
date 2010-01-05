/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Histograms.
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

#include <limits.h>
#include <math.h>
#include <string.h>
#include <glib.h>

#include "pgm/histogram.h"


//#define HISTOGRAM_DEBUG

#ifndef HISTOGRAM_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


GSList* pgm_histograms = NULL;


static void sample_set_accumulate (pgm_sample_set_t*, pgm_sample_t, pgm_count_t, gsize);
static pgm_count_t sample_set_total_count (pgm_sample_set_t*);

static void set_bucket_range (pgm_histogram_t*, gsize, pgm_sample_t);
static void initialize_bucket_range (pgm_histogram_t*);
static gsize bucket_index (pgm_histogram_t*, pgm_sample_t);
static void accumulate (pgm_histogram_t*, pgm_sample_t, pgm_count_t, gsize);
static double get_peak_bucket_size (pgm_histogram_t*, pgm_sample_set_t*);
static double get_bucket_size (pgm_histogram_t*, pgm_count_t, gsize);

static void pgm_histogram_write_html_graph (pgm_histogram_t*, GString*);
static void write_ascii (pgm_histogram_t*, const gchar* newline, GString*);
static void write_ascii_header (pgm_histogram_t*, pgm_sample_set_t*, pgm_count_t, GString*);
static void write_ascii_bucket_graph (double, double, GString*);
static void write_ascii_bucket_context (gint64, pgm_count_t, gint64, gsize, GString*);
static void write_ascii_bucket_value (pgm_count_t, double, GString*);
static GString* get_ascii_bucket_range (pgm_histogram_t*, gsize);


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
	const gsize i = bucket_index (histogram, value);
	g_assert (value >= histogram->ranges[ i ]);
	g_assert (value  < histogram->ranges[ i + 1 ]);
	accumulate (histogram, value, 1, i);
}

void
pgm_histogram_write_html_graph_all (
	GString*		string
	)
{
	if (!pgm_histograms)
		return;
	GSList* snapshot = pgm_histograms;
	while (snapshot) {
		pgm_histogram_t* histogram = snapshot->data;
		pgm_histogram_write_html_graph (histogram, string);
		snapshot = snapshot->next;
	}
}

static
void
pgm_histogram_write_html_graph (
	pgm_histogram_t*	histogram,
	GString*		string
	)
{
	g_string_append (string, "<PRE>");
	write_ascii (histogram, "<BR/>", string);
	g_string_append (string, "</PRE>");
}

static
void
sample_set_accumulate (
	pgm_sample_set_t*	sample_set,
	pgm_sample_t		value,
	pgm_count_t		count,
	gsize			i
	)
{
	g_assert (1 == count || -1 == count);
	sample_set->counts[ i ] += count;
	sample_set->sum += count * value;
	sample_set->square_sum += (count * value) * (gint64)value;
	g_assert (sample_set->counts[ i ] >= 0);
	g_assert (sample_set->sum >= 0);
	g_assert (sample_set->square_sum >= 0);
}

static
pgm_count_t
sample_set_total_count (
	pgm_sample_set_t*	sample_set
	)
{
	pgm_count_t total = 0;
	for (gint i = 0; i < sample_set->counts_len; i++)
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
	g_assert (histogram->declared_min > 0);
	histogram->declared_max = INT_MAX - 1;
	g_assert (histogram->declared_min <= histogram->declared_max);
	g_assert (1 < histogram->bucket_count);
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
	gsize			i,
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
	double log_max = log(histogram->declared_max);
	double log_ratio;
	double log_next;
	gsize i = 1;
	pgm_sample_t current = histogram->declared_min;

	set_bucket_range (histogram, i, current);
	while (histogram->bucket_count > ++i) {
		double log_current = log(current);
		log_ratio = (log_max - log_current) / (histogram->bucket_count - i);
		log_next = log_current + log_ratio;
		int next = floor(exp(log_next) + 0.5);
		if (next > current)
			current = next;
		else
			current++;
		set_bucket_range (histogram, i, current);
	}
	g_assert (histogram->bucket_count == i);
}

static
gsize
bucket_index (
	pgm_histogram_t*	histogram,
	pgm_sample_t		value
	)
{
	g_assert (histogram->ranges[0] <= value);
	g_assert (histogram->ranges[ histogram->bucket_count ] > value);
	gsize under = 0;
	gsize over = histogram->bucket_count;
	gsize mid;

	do {
		g_assert (over >= under);
		mid = ((unsigned int)under + (unsigned int)over) >> 1;
		if (mid == under)
			break;
		if (histogram->ranges[ mid ] <= value)
			under = mid;
		else
			over = mid;
	} while (TRUE);
	g_assert (histogram->ranges[ mid ] <= value &&
		  histogram->ranges[ mid + 1] > value);
	return mid;
}

static
void
accumulate (
	pgm_histogram_t*	histogram,
	pgm_sample_t		value,
	pgm_count_t		count,
	gsize			i
	)
{
	sample_set_accumulate (&histogram->sample, value, count, i);
}

static
void
write_ascii (
	pgm_histogram_t*	histogram,
	const gchar*		newline,
	GString*		output
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
	g_string_append (output, newline);

	double max_size = get_peak_bucket_size (histogram, &snapshot);
	gsize largest_non_empty_bucket = histogram->bucket_count - 1;
	while (0 == snapshot.counts[ largest_non_empty_bucket ])
	{
		if (0 == largest_non_empty_bucket)
			break;
		largest_non_empty_bucket--;
	}

	gsize print_width = 1;
	for (gsize i = 0; i < histogram->bucket_count; ++i)
	{
		if (snapshot.counts[ i ]) {
			GString* bucket_range = get_ascii_bucket_range (histogram, i);
			gsize width = bucket_range->len + 1;
			g_string_free (bucket_range, TRUE);
			if (width > print_width)
				print_width = width;
		}
	}

	gint64 remaining = sample_count;
	gint64 past = 0;
	for (gsize i = 0; i < histogram->bucket_count; ++i)
	{
		pgm_count_t current = snapshot.counts[ i ];
		remaining -= current;
		GString* bucket_range = get_ascii_bucket_range (histogram, i);
		g_string_append_printf (output, "%*s ", (int)print_width, bucket_range->str);
		g_string_free (bucket_range, TRUE);
		if (0 == current &&
		    i < histogram->bucket_count - 1 &&
		    0 == snapshot.counts[ i + 1 ])
		{
			while (i < histogram->bucket_count - 1 &&
			       0 == snapshot.counts[ i + 1 ])
			{
				i++;
			}
			g_string_append (output, "... ");
			g_string_append (output, newline);
			continue;
		}

		double current_size = get_bucket_size (histogram, current, i);
		write_ascii_bucket_graph (current_size, max_size, output);
		write_ascii_bucket_context (past, current, remaining, i, output);
		g_string_append (output, newline);
		past += current;
	}
}

static
void
write_ascii_header (
	pgm_histogram_t*	histogram,
	pgm_sample_set_t*	sample_set,
	pgm_count_t		sample_count,
	GString*		output
	)
{
	g_string_append_printf (output,
				 "Histogram: %s recorded %ld samples",
				 histogram->histogram_name,
				 (long)sample_count);
	if (sample_count > 0) {
		double average = sample_set->sum / sample_count;
		double variance = sample_set->square_sum / sample_count
				  - average * average;
		double standard_deviation = sqrt (variance);
		g_string_append_printf (output,
					 ", average = %.1f, standard deviation = %.1f",
					 average, standard_deviation);
	}
}

static
void
write_ascii_bucket_graph (
	double			current_size,
	double			max_size,
	GString*		output
	)
{
	const int k_line_length = 72;
	int x_count = (k_line_length * (current_size / max_size) + 0.5);
	int x_remainder = k_line_length - x_count;
	while (0 < x_count--)
		g_string_append_c (output, '-');
	g_string_append_c (output, 'O');
	while (0 < x_remainder--)
		g_string_append_c (output, ' ');
}

static
void
write_ascii_bucket_context (
	gint64			past,
	pgm_count_t		current,
	gint64			remaining,
	gsize			i,
	GString*		output
	)
{
	double scaled_sum = (past + current + remaining) / 100.0;
	write_ascii_bucket_value (current, scaled_sum, output);
	if (0 < i) {
		double percentage = past / scaled_sum;
		g_string_append_printf (output, " {%3.1f%%}", percentage);
	}
}

static
void
write_ascii_bucket_value (
	pgm_count_t		current,
	double			scaled_sum,
	GString*		output
	)
{
	g_string_append_printf (output, " (%d = %3.1f%%)", current, current/scaled_sum);
}

static
double
get_peak_bucket_size (
	pgm_histogram_t*	histogram,
	pgm_sample_set_t*	sample_set
	)
{
	double max = 0;
	for (gsize i = 0; i < histogram->bucket_count; i++) {
		double current_size = get_bucket_size (histogram, sample_set->counts[ i ], i);
		if (current_size > max)
			max = current_size;
	}
	return max;
}

static
double
get_bucket_size (
	pgm_histogram_t*	histogram,
	pgm_count_t		current,
	gsize			i
	)
{
	g_assert (histogram->ranges[ i + 1 ] > histogram->ranges[ i ]);
	static const double kTransitionWidth = 5;
	double denominator = histogram->ranges[ i + 1 ] - histogram->ranges[ i ];
	if (denominator > kTransitionWidth)
		denominator = kTransitionWidth;
	return current / denominator;
}

static
GString*
get_ascii_bucket_range (
	pgm_histogram_t*	histogram,
	gsize			i
	)
{
	GString* result = g_string_new (NULL);
	g_string_printf (result, "%d", histogram->ranges[ i ]);
	return result;
}

/* eof */

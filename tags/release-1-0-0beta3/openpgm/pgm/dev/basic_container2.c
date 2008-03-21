/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test basic containers for access performance.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>


#include <glib.h>


struct tests {
	double (*test_func)(int, int);
	char*	name;
};


/* globals */

double test_control (int, int);
double test_alloc_list (int, int);
double test_alloc_list_malloc (int, int);
double test_alloc_list_stack (int, int);
double test_alloc_slist (int, int);
double test_alloc_queue (int, int);
double test_alloc_hash (int, int);
double test_alloc_ptr_array (int, int);
double test_alloc_byte_array (int, int);

#if HAVE_GLIB_SEQUENCE
double test_alloc_sequence (int, int);
#endif

static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("basic_container2");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "h")) != -1)
	{
		switch (c) {

		case 'h':
		case '?': usage (binary_name);
		}
	}

/* setup signal handlers */
	signal(SIGHUP, SIG_IGN);

	int test_size[] = { 100000, 200000, 100000, 200000, 0 };
	int test_payload[] = { /*9000,*/ 1500, 0 };
	struct tests tests[] = {
//			{ test_control, "control" },
			{ test_alloc_list, "list/slice" },
			{ test_alloc_slist, "slist" },
			{ test_alloc_hash, "hash" },
			{ test_alloc_queue, "queue" },
			{ test_alloc_ptr_array, "*array" },
			{ test_alloc_byte_array, "byte array" },
#if HAVE_GLIB_SEQUENCE
			{ test_alloc_sequence, "sequence" },
#endif
			{ NULL, NULL }
			};

/* print header */
	struct tests* p;
	int* p2;
	int* p3;

	int test_count = 3;

	p = tests;
	do {
		p2 = test_payload;
		do {
			for (int c = 1; c <= test_count; c++)
			{
				printf (",%s@%ib/%i", p->name, *p2, c);
			}
		} while (*(++p2));
	} while ((++p)->name);
	putchar ('\n');

/* each row is one payload size */
	p3 = test_size;
	do {
		printf ("%i", *p3);

		p = tests;
		do {
			p2 = test_payload;
			do {
				for (int c = 1; c <= 3; c++)
				{
					printf (",%g", p->test_func (*p3, *p2) );
				}
			} while (*(++p2));
		} while ((++p)->name);

		putchar ('\n');
	} while (*(++p3));

	puts ("finished.");
	return 0;
}

void
_list_iterator (
		gpointer data,
		gpointer user_data
		)
{
	if ( *(int*)user_data )
		g_slice_free1 ( *(int*)user_data, data );
}

void
_list_free_iterator (
		gpointer data,
		gpointer user_data
		)
{
	if ( *(int*)user_data )
		free (data);
}

double
test_control (
		int count,
		int size_per_entry
		)
{
#if 1
	char *p = g_slice_alloc (100);
	g_slice_free1 (100, p);
#else
	char *p = malloc (100);
	free (p);
#endif

	return 0.0;
}

double
test_alloc_list (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GList *list = NULL;
	int i;

/* fill up container */
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		list = g_list_prepend (list, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = g_list_nth_data (list, i);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_list_foreach (list, _list_iterator, &size_per_entry);
	g_list_free (list);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_slist (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GSList *list = NULL;
	int i;

/* fill up container */
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		list = g_slist_prepend (list, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = g_slist_nth_data (list, i);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_slist_foreach (list, _list_iterator, &size_per_entry);
	g_slist_free (list);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_queue (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GQueue* queue = NULL;
	int i;

/* fill up container */
	queue = g_queue_new ();
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		g_queue_push_head (queue, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = (char*)g_queue_peek_nth (queue, i) + sizeof(int);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_queue_foreach (queue, _list_iterator, &size_per_entry);
	g_queue_free (queue);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

#if HAVE_GLIB_SEQUENCE
double
test_alloc_sequence (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GSequence* sequence = NULL;
	int i;

/* fill up container */
	sequence = g_sequence_new (NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		g_sequence_append (p, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		GSequenceIter* it = g_sequence_get_iter_at_pos (i);
		char *packet = g_sequence_get (it);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_sequence_foreach (sequence, _list_iterator, &size_per_entry);
	g_sequence_free (sequence);

	return (secs * 1000.0 * 1000.0) / (double)count;
}
#endif

void
_hash_iterator (
		gpointer key,
		gpointer value,
		gpointer user_data
		)
{
	g_slice_free1 ( *(int*)user_data + sizeof(int), value );
}

double
test_alloc_hash (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GHashTable* hash = NULL;
	int i;

/* fill up container */
	hash = g_hash_table_new (g_int_hash, g_int_equal);
	for (i = 0; i < count; i++)
	{
/* we add an integer to every datum for use as the hash key */
		char *entry = g_slice_alloc (size_per_entry + sizeof(int));
		int *key = (int*)entry;
		*key = i;
		g_hash_table_insert (hash, key, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = g_hash_table_lookup (hash, &i);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_hash_table_foreach (hash, _hash_iterator, &size_per_entry);
	g_hash_table_destroy (hash);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_ptr_array (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GPtrArray* array = NULL;
	int i;

/* fill up container */
	array = g_ptr_array_new ();
	for (i = 0; i < count; i++)
	{
		char *entry = g_slice_alloc (size_per_entry);
		g_ptr_array_add (array, entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = g_ptr_array_index (array, i);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	if (size_per_entry)
		g_ptr_array_foreach (array, _list_iterator, &size_per_entry);
	g_ptr_array_free (array, TRUE);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_byte_array (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GByteArray* array = NULL;
	guint8 *data = NULL;
	int i;

/* fill up container */
	array = g_byte_array_new ();
	data = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
	for (i = 0; i < count; i++)
	{
		g_byte_array_append (array, data, size_per_entry);
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet = &array->data[size_per_entry * i];
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	g_byte_array_free (array, TRUE);
	if (size_per_entry)
		g_slice_free1 (size_per_entry, data);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */

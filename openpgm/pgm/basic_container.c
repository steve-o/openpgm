/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test basic containers for alloc performance.
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
	puts ("basic_container");

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

	int test_size[] = { 10, 20, 100, 200, 1000, 2000, 10000, 20000, 100000, 200000, 0 };
	int test_payload[] = { 9000, 1500, 0 };
	struct tests tests[] = {
//			{ test_alloc_list_malloc, "list/malloc" },
//			{ test_alloc_list, "list/slice" },
			{ test_alloc_list_stack, "list/stack" },
//			{ test_alloc_slist, "slist" },
//			{ test_alloc_hash, "hash" },
//			{ test_alloc_queue, "queue" },
//			{ test_alloc_ptr_array, "*array" },
//			{ test_alloc_byte_array, "byte array" },
#if HAVE_GLIB_SEQUENCE
			{ test_alloc_sequence, "sequence" },
#endif
			{ NULL, NULL }
			};
	struct tests* p2;

	p2 = tests;
	do {
		int *p3 = test_payload;

		do {
			printf ("%s@%i bytes\n", p2->name, *p3);

			int *p = test_size; do { printf ("%i,", *p++); } while (*p); p = test_size;
			putchar ('\n');

			do {
				p2->test_func (*p, *p3);
				double result = p2->test_func (*p, *p3);
				printf ("%g,", result);
				fflush (stdout);
			} while (*(++p));
			putchar ('\n');

		} while (*(++p3));

	} while ((++p2)->name);
		
/* with payload */

	puts ("finished.");
	return 0;
}

void
_list_iterator (
		gpointer data,
		gpointer user_data
		)
{
	g_slice_free1 ( *(int*)user_data, data );
}

double
test_alloc_list (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GList *list = NULL, *p = list;
	int i;

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		list = g_list_prepend (list, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	g_list_foreach (list, _list_iterator, &size_per_entry);
	g_list_free (list);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_list_malloc (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GList *list = NULL, *p = list;
	int i;

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_malloc (size_per_entry) : NULL;
		list = g_list_prepend (list, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	g_list_foreach (list, _list_iterator, &size_per_entry);
	g_list_free (list);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_alloc_list_stack (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	GList *list = NULL, *p = list;
	GTrashStack *stack = NULL;
	int i;

	if (size_per_entry)
	{
		for (i = 0; i < count; i++)
		{
			char *entry = g_slice_alloc (size_per_entry);
			g_trash_stack_push (&stack, entry);
		}
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_trash_stack_pop (&stack) : NULL;
		list = g_list_prepend (list, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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
	GSList *list = NULL, *p = list;
	int i;

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		list = g_slist_prepend (list, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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

	queue = g_queue_new ();

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		g_queue_push_head (queue, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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

	sequence = g_sequence_new (NULL);

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		g_sequence_append (p, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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
	g_slice_free ( int, key );
	g_slice_free1 ( *(int*)user_data, value );
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

	hash = g_hash_table_new (g_int_hash, g_int_equal);

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		int *key = g_slice_new (int);
		g_hash_table_insert (hash, key, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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

	array = g_ptr_array_new ();

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;
		g_ptr_array_add (array, entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

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

	array = g_byte_array_new ();
	data = size_per_entry ? g_slice_alloc (size_per_entry) : NULL;

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		g_byte_array_append (array, data, size_per_entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	g_byte_array_free (array, TRUE);
	g_slice_free1 (size_per_entry, data);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */

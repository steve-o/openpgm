/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * mutexes and locks.
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

#ifndef __PGM_THREAD_H__
#define __PGM_THREAD_H__

#include <glib.h>


struct pgm_mutex_t {
#ifdef G_OS_UNIX
	pthread_mutex_t		pthread_mutex;
#else
	CRITICAL_SECTION	win32_mutex;
#endif /* !G_OS_UNIX */
};

typedef struct pgm_mutex_t pgm_mutex_t;

struct pgm_cond_t {
#ifdef G_OS_UNIX
	pthread_cond_t		pthread_cond;
#elif defined(CONFIG_HAVE_WIN_COND)
	CONDITION_VARIABLE	win32_cond;
#else
	CRITICAL_SECTION	win32_mutex;
	GPtrArray		array;
#endif /* !G_OS_UNIX */
};

typedef struct pgm_cond_t pgm_cond_t;

struct pgm_rw_lock_t {
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	SRWLOCK		win32_lock;
#else
	pgm_mutex_t	mutex;
	pgm_cond_t	read_cond;
	pgm_cond_t	write_cond;
	guint		read_counter;
	gboolean	have_writer;
	guint		want_to_read;
	guint		want_to_write;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
};

typedef struct pgm_rw_lock_t pgm_rw_lock_t;

G_BEGIN_DECLS

void pgm_mutex_init (pgm_mutex_t*);
void pgm_mutex_lock (pgm_mutex_t*);
gboolean pgm_mutex_trylock (pgm_mutex_t*);
void pgm_mutex_unlock (pgm_mutex_t*);
void pgm_mutex_free (pgm_mutex_t*);

void pgm_cond_init (pgm_cond_t*);
void pgm_cond_signal (pgm_cond_t*);
void pgm_cond_broadcast (pgm_cond_t*);
void pgm_cond_wait (pgm_cond_t*, pgm_mutex_t*);
void pgm_cond_free (pgm_cond_t*);

void pgm_rw_lock_init (pgm_rw_lock_t*);
void pgm_rw_lock_reader_lock (pgm_rw_lock_t*);
gboolean pgm_rw_lock_reader_trylock (pgm_rw_lock_t*);
void pgm_rw_lock_reader_unlock(pgm_rw_lock_t*);
void pgm_rw_lock_writer_lock (pgm_rw_lock_t*);
gboolean pgm_rw_lock_writer_trylock (pgm_rw_lock_t*);
void pgm_rw_lock_writer_unlock (pgm_rw_lock_t*);
void pgm_rw_lock_free (pgm_rw_lock_t*);

void pgm_thread_init (void);
void pgm_thread_shutdown (void);

G_END_DECLS

#endif /* __PGM_THREAD_H__ */

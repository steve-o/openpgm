/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic message reporting.
 *
 * Copyright (c) 2010 Miru Limited.
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

#ifndef __PGM_MESSAGES_H__
#define __PGM_MESSAGES_H__

#include <glib.h>


G_BEGIN_DECLS

/* Set bitmask of log roles in environmental variable PGM_LOG_MASK,
 * borrowed from SmartPGM.
 */
enum {
	PGM_LOG_ROLE_MEMORY		= 0x001,
	PGM_LOG_ROLE_NETWORK		= 0x002,
	PGM_LOG_ROLE_CONFIGURATION	= 0x004,
	PGM_LOG_ROLE_SESSION		= 0x010,
	PGM_LOG_ROLE_NAK		= 0x020,
	PGM_LOG_ROLE_RATE_CONTROL	= 0x040,
	PGM_LOG_ROLE_TX_WINDOW		= 0x080,
	PGM_LOG_ROLE_RX_WINDOW		= 0x100,
	PGM_LOG_ROLE_FEC		= 0x400,
	PGM_LOG_ROLE_CONGESTION_CONTROL	= 0x800
};

enum {
	PGM_LOG_LEVEL_DEBUG	= 0,
	PGM_LOG_LEVEL_TRACE	= 1,
	PGM_LOG_LEVEL_MINOR	= 2,
	PGM_LOG_LEVEL_NORMAL	= 3,
	PGM_LOG_LEVEL_WARNING	= 4,
	PGM_LOG_LEVEL_ERROR	= 5,
	PGM_LOG_LEVEL_FATAL	= 6
};

extern int pgm_log_mask;
extern int pgm_min_log_level;

typedef void (*pgm_log_func_t) (const int, const char*, void*);

pgm_log_func_t pgm_log_set_handler (pgm_log_func_t, void*);
void pgm__log (const int, const char*, ...) G_GNUC_PRINTF (2, 3);
void pgm__logv (const int, const char*, va_list);

#ifdef G_HAVE_ISO_VARARGS

#define pgm_debug(...)		if (pgm_min_log_level == PGM_LOG_LEVEL_DEBUG) pgm__log (PGM_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define pgm_trace(r,...)	if (pgm_min_log_level <= PGM_LOG_LEVEL_TRACE && pgm_log_mask & (r)) \
					pgm__log (PGM_LOG_LEVEL_TRACE, __VA_ARGS__)
#define pgm_minor(...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_MINOR) pgm__log (PGM_LOG_LEVEL_MINOR, __VA_ARGS__)
#define pgm_info(...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_NORMAL) pgm__log (PGM_LOG_LEVEL_NORMAL, __VA_ARGS__)
#define pgm_warn(...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_WARNING) pgm__log (PGM_LOG_LEVEL_WARNING, __VA_ARGS__)
#define pgm_error(...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_ERROR) pgm__log (PGM_LOG_LEVEL_ERROR, __VA_ARGS__)
#define pgm_fatal(...)		pgm__log (PGM_LOG_LEVEL_FATAL, __VA_ARGS__)

#elif defined(G_HAVE_GNUC_VARARGS)

#define pgm_debug(f...)		if (pgm_min_log_level == PGM_LOG_LEVEL_DEBUG) pgm__log (PGM_LOG_LEVEL_DEBUG, f)
#define pgm_trace(r,f...)	if (pgm_min_log_level <= PGM_LOG_LEVEL_TRACE && pgm_log_mask & (r)) \
					pgm__log (PGM_LOG_LEVEL_TRACE, f)
#define pgm_minor(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_MINOR) pgm__log (PGM_LOG_LEVEL_MINOR, f)
#define pgm_info(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_NORMAL) pgm__log (PGM_LOG_LEVEL_NORMAL, f)
#define pgm_warn(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_WARNING) pgm__log (PGM_LOG_LEVEL_WARNING, f)
#define pgm_error(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_ERROR) pgm__log (PGM_LOG_LEVEL_ERROR, f)
#define pgm_fatal(f...)		pgm__log (PGM_LOG_LEVEL_FATAL, f)

#else   /* no varargs macros */

static inline void pgm_debug (const char* format, ...) {
	if (PGM_LOG_LEVEL_DEBUG == pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_DEBUG, format, args);
		va_end (args);
	}
}

static inline void pgm_trace (const int role, const char* format, ...) {
	if (PGM_LOG_LEVEL_TRACE => pgm_min_log_level && pgm_log_mask & role) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_TRACE, format, args);
		va_end (args);
	}
}

static inline void pgm_minor (const char* format, ...) {
	if (PGM_LOG_LEVEL_MINOR => pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_MINOR, format, args);
		va_end (args);
	}
}

static inline void pgm_info (const char* format, ...) {
	if (PGM_LOG_LEVEL_NORMAL => pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_NORMAL, format, args);
		va_end (args);
	}
}

static inline void pgm_warn (const char* format, ...) {
	if (PGM_LOG_LEVEL_WARNING => pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_WARNING, format, args);
		va_end (args);
	}
}

static inline void pgm_error (const char* format, ...) {
	if (PGM_LOG_LEVEL_ERROR => pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_WARNING, format, args);
		va_end (args);
	}
}

static inline void pgm_fatal (const char* format, ...) {
	va_list args;
	va_start (args, format);
	pgm__logv (PGM_LOG_LEVEL_FATAL, format, args);
	va_end (args);
}

#endif /* varargs */

#define pgm_warn_if_reached() \
	do { \
		pgm_warn ("file %s: line %d (%s): code should not be reached", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__); \
	} while (0)
#define pgm_warn_if_fail(expr) \
	do { \
		if (G_LIKELY (expr)); \
		else \
			pgm_warn ("file %s: line %d (%s): runtime check failed: (%s)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
	} while (0)


#ifdef PGM_DISABLE_ASSERT

#	define pgm_assert(expr)			while (0)
#	define pgm_assert_not_reached()		while (0)
#	define pgm_assert_cmpint(n1, cmp, n2)	while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2)	while (0)

#elif defined(__GNUC__)

#	define pgm_assert(expr) \
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed: (%s)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_not_reached() \
	do { \
		pgm_fatal ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		abort (); \
	} while (0)
#	define pgm_assert_cmpint(n1, cmp, n2) \
	do { \
		const int _n1 = (n1), _n2 = (n2); \
		if (G_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed (%s): (%u %s %u)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2) \
	do { \
		const unsigned _n1 = (n1), _n2 = (n2); \
		if (G_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed (%s): (%u %s %u)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)

#else

#	define pgm_assert(expr) \
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed: (%s)", \
				__FILE__, __LINE__, #expr); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_not_reached() \
	do { \
		pgm_fatal ("file %s: line %d: assertion failed: (%s)", \
			__FILE__, __LINE__); \
		abort (); \
	} while (0)
#	define pgm_assert_cmpint(n1, cmp, n2) \
	do { \
		const int _n1 = (n1), _n2 = (n2); \
		if (G_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed (%s): (%u %s %u)", \
				__FILE__, __LINE__, #expr, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2) \
	do { \
		const unsigned _n1 = (n1), _n2 = (n2); \
		if (G_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed (%s): (%u %s %u)", \
				__FILE__, __LINE__, #expr, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)

#endif /* !PGM_DISABLE_ASSERT */

#ifdef PGM_DISABLE_CHECKS

#	define pgm_return_if_fail(expr)			while (0)
#	define pgm_return_val_if_fail(expr, val)	while (0)
#	define pgm_return_if_reached()			return
#	define pgm_return_val_if_reached(val)		return (val)

#elif defined(__GNUC__)

#	define pgm_return_if_fail(expr)	\
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d (%s): assertion `%s' failed", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			return; \
		} \
	} while (0)
#	define pgm_return_val_if_fail(expr, val) \
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d (%s): assertion `%s' failed", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			return (val); \
		} \
	} while (0)
#	define pgm_return_if_reached() \
	do { \
		pgm_warn ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		return; \
	} while (0)
#	define pgm_return_val_if_reached(val) \
	do { \
		pgm_warn ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		return (val); \
	} while (0)

#else

#	define pgm_return_if_fail(expr)	\
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d: assertion `%s' failed", \
				__FILE__, __LINE__, #expr); \
			return; \
		} \
	} while (0)
#	define pgm_return_val_if_fail(expr, val) \
	do { \
		if (G_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d: assertion `%s' failed", \
				__FILE__, __LINE__, #expr); \
			return (val); \
		} \
	} while (0)
#	define pgm_return_if_reached() \
	do { \
		pgm_warn ("file %s: line %d): should not be reached", \
			__FILE__, __LINE__); \
		return; \
	} while (0)
#	define pgm_return_val_if_reached(val) \
	do { \
		pgm_warn ("file %s: line %d: should not be reached", \
			__FILE__, __LINE__); \
		return (val); \
	} while (0)

#endif /* !PGM_DISABLE_CHECKS */


void pgm_messages_init (void);
void pgm_messages_shutdown (void);

G_END_DECLS

#endif /* __PGM_MESSAGES_H__ */

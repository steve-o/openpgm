/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * HTTP administrative interface
 *
 * Copyright (c) 2006-2010 Miru Limited.
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
#include <inttypes.h>
#ifndef _WIN32
#	include <netdb.h>
#else
#	include <io.h>
#	include <lmcons.h>
#	include <process.h>
#endif
#include <stdio.h>
#include <time.h>
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/receiver.h>
#include <impl/socket.h>
#include <pgm/if.h>
#include <pgm/version.h>

#include "pgm/http.h"
#include "htdocs/404.html.h"
#include "htdocs/base.css.h"
#include "htdocs/robots.txt.h"
#include "htdocs/xhtml10_strict.doctype.h"


/* OpenSolaris */
#ifndef LOGIN_NAME_MAX
#	ifdef _WIN32
#		define LOGIN_NAME_MAX		(UNLEN + 1)
#	else
#		define LOGIN_NAME_MAX		256
#	endif
#endif

#ifdef _WIN32
#	define getpid		_getpid
#	define read		_read
#	define write		_write
#	define SHUT_WR		SD_SEND
#endif

#ifdef HAVE_SPRINTF_GROUPING
#	define GROUP_FORMAT			"'"
#else
#	define GROUP_FORMAT			""
#endif

#define HTTP_BACKLOG			10 /* connections */
#define HTTP_TIMEOUT			60 /* seconds */


/* locals */

struct http_connection_t {
	pgm_list_t	link_;
	SOCKET		sock;
	enum {
		HTTP_STATE_READ,
		HTTP_STATE_WRITE,
		HTTP_STATE_FINWAIT
	}		state;

	char*		buf;
	size_t		buflen;
	size_t		bufoff;
	unsigned	status_code;
	const char*	status_text;
	const char*	content_type;
};

enum {
	HTTP_MEMORY_STATIC,
	HTTP_MEMORY_TAKE
};

static char		http_hostname[NI_MAXHOST];
static char		http_address[INET6_ADDRSTRLEN];
static char		http_username[LOGIN_NAME_MAX + 1];
static int		http_pid;

static SOCKET			http_sock = INVALID_SOCKET;
#ifndef _WIN32
static pthread_t		http_thread;
static void*			http_routine (void*);
#else
static HANDLE			http_thread;
static unsigned __stdcall	http_routine (void*);
#endif
static SOCKET			http_max_sock = INVALID_SOCKET;
static fd_set			http_readfds, http_writefds, http_exceptfds;
static pgm_list_t*		http_socks = NULL;
static pgm_notify_t		http_notify = PGM_NOTIFY_INIT;
static volatile uint32_t	http_ref_count = 0;


static int http_tsi_response (struct http_connection_t*restrict, const pgm_tsi_t*restrict);
static void http_each_receiver (const pgm_sock_t*restrict, const pgm_peer_t*restrict, pgm_string_t*restrict);
static int http_receiver_response (struct http_connection_t*restrict, const pgm_sock_t*restrict, const pgm_peer_t*restrict);

static void default_callback (struct http_connection_t*restrict, const char*restrict);
static void robots_callback (struct http_connection_t*restrict, const char*restrict);
static void css_callback (struct http_connection_t*restrict, const char*restrict);
static void index_callback (struct http_connection_t*restrict, const char*restrict);
static void interfaces_callback (struct http_connection_t*restrict, const char*restrict);
static void transports_callback (struct http_connection_t*restrict, const char*restrict);
static void histograms_callback (struct http_connection_t*restrict, const char*restrict);

static struct {
	const char*	path;
	void	      (*callback) (struct http_connection_t*restrict, const char*restrict);
} http_directory[] = {
	{ "/robots.txt",	robots_callback },
	{ "/base.css",		css_callback },
	{ "/",			index_callback },
	{ "/interfaces",	interfaces_callback },
	{ "/transports",	transports_callback }
#ifdef USE_HISTOGRAMS
       ,{ "/histograms",	histograms_callback }
#endif
};


static
int
http_sock_rcvtimeo (
	SOCKET			sock,
	int			seconds
	)
{
#if defined( __sun )
	return 0;
#elif !defined( _WIN32 )
	const struct timeval timeout = { .tv_sec = seconds, .tv_usec = 0 };
	return setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
	const int optval = seconds * 1000;
	return setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&optval, sizeof(optval));
#endif
}

static
int
http_sock_sndtimeo (
	SOCKET			sock,
	int			seconds
	)
{
#if defined( __sun )
	return 0;
#elif !defined( _WIN32 )
	const struct timeval timeout = { .tv_sec = seconds, .tv_usec = 0 };
	return setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
	const int optval = seconds * 1000;
	return setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&optval, sizeof(optval));
#endif
}

bool
pgm_http_init (
	in_port_t		http_port,
	pgm_error_t**		error
	)
{
	int e;

	if (pgm_atomic_exchange_and_add32 (&http_ref_count, 1) > 0)
		return TRUE;

/* resolve and store relatively constant runtime information */
	if (0 != gethostname (http_hostname, sizeof (http_hostname))) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_sock_errno (save_errno),
			     _("Resolving hostname: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
	http_hostname[NI_MAXHOST - 1] = '\0';
	struct addrinfo hints = {
		.ai_family	= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= IPPROTO_TCP,
		.ai_flags	= AI_ADDRCONFIG
	}, *res = NULL;
	e = getaddrinfo (http_hostname, NULL, &hints, &res);
	if (0 != e) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_eai_errno (e, errno),
			     _("Resolving hostname address: %s"),
			     pgm_gai_strerror_s (errbuf, sizeof (errbuf), e));
		goto err_cleanup;
	}
/* NB: getaddrinfo may return multiple addresses, one per interface & family, only the first
 * return result is used.  The sorting order of the list defined by RFC 3484 and /etc/gai.conf
 */
	e = getnameinfo (res->ai_addr, res->ai_addrlen,
		         http_address, sizeof(http_address),
			 NULL, 0,
			 NI_NUMERICHOST);
	if (0 != e) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_eai_errno (e, errno),
			     _("Resolving numeric hostname: %s"),
			     pgm_gai_strerror_s (errbuf, sizeof (errbuf), e));
		goto err_cleanup;
	}
	freeaddrinfo (res);
#ifndef _WIN32
	e = getlogin_r (http_username, sizeof(http_username));
	if (0 != e) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_errno (errno),
			     _("Retrieving user name: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		goto err_cleanup;
	}
#else
	wchar_t wusername[UNLEN + 1];
	DWORD nSize = PGM_N_ELEMENTS( wusername );
	if (!GetUserNameW (wusername, &nSize)) {
		const DWORD save_errno = GetLastError();
		char winstr[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_win_errno (save_errno),
			     _("Retrieving user name: %s"),
			     pgm_win_strerror (winstr, sizeof (winstr), save_errno));
		goto err_cleanup;
	}
	WideCharToMultiByte (CP_UTF8, 0, wusername, nSize + 1, http_username, sizeof(http_username), NULL, NULL);
#endif /* _WIN32 */
	http_pid = getpid();

/* create HTTP listen socket */
	if (INVALID_SOCKET == (http_sock = socket (AF_INET,  SOCK_STREAM, 0))) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_HTTP,
				pgm_error_from_sock_errno (save_errno),
				_("Creating HTTP socket: %s"),
				pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
	const int v = 1;
	if (0 != setsockopt (http_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v))) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_HTTP,
				pgm_error_from_sock_errno (save_errno),
				_("Enabling reuse of socket local address: %s"),
				pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
	if (0 != http_sock_rcvtimeo (http_sock, HTTP_TIMEOUT) ||
	    0 != http_sock_sndtimeo (http_sock, HTTP_TIMEOUT)) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_HTTP,
				pgm_error_from_sock_errno (save_errno),
				_("Setting socket timeout: %s"),
				pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
	struct sockaddr_in http_addr;
	memset (&http_addr, 0, sizeof(http_addr));
	http_addr.sin_family = AF_INET;
	http_addr.sin_addr.s_addr = INADDR_ANY;
	http_addr.sin_port = htons (http_port);
	if (0 != bind (http_sock, (struct sockaddr*)&http_addr, sizeof(http_addr))) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&http_addr, addr, sizeof(addr));
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_sock_errno (save_errno),
			     _("Binding HTTP socket to address %s: %s"),
			     addr,
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
	if (0 != listen (http_sock, HTTP_BACKLOG)) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_sock_errno (save_errno),
			     _("Listening to HTTP socket: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}

/* non-blocking notification of new connections */
	pgm_sockaddr_nonblocking (http_sock, TRUE);

/* create notification channel */
	if (0 != pgm_notify_init (&http_notify)) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_sock_errno (save_errno),
			     _("Creating HTTP notification channel: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}

/* spawn thread to handle HTTP requests */
#ifndef _WIN32
	const int status = pthread_create (&http_thread, NULL, &http_routine, NULL);
	if (0 != status) {
		const int save_errno = errno;
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_errno (save_errno),
			     _("Creating HTTP thread: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
#else
	http_thread = (HANDLE)_beginthreadex (NULL, 0, &http_routine, NULL, 0, NULL);
	if (0 == http_thread) {
		const int save_errno = errno;
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_HTTP,
			     pgm_error_from_errno (save_errno),
			     _("Creating HTTP thread: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto err_cleanup;
	}
#endif /* _WIN32 */
	pgm_minor (_("Web interface: http://%s:%i"),
			http_hostname,
			http_port);
	return TRUE;

err_cleanup:
	if (INVALID_SOCKET != http_sock) {
		closesocket (http_sock);
		http_sock = INVALID_SOCKET;
	}
	if (pgm_notify_is_valid (&http_notify)) {
		pgm_notify_destroy (&http_notify);
	}
	pgm_atomic_dec32 (&http_ref_count);
	return FALSE;
}

/* notify HTTP thread to shutdown, wait for shutdown and cleanup.
 */

bool
pgm_http_shutdown (void)
{
	pgm_return_val_if_fail (pgm_atomic_read32 (&http_ref_count) > 0, FALSE);

	if (pgm_atomic_exchange_and_add32 (&http_ref_count, (uint32_t)-1) != 1)
		return TRUE;

	pgm_notify_send (&http_notify);
#ifndef _WIN32
	pthread_join (http_thread, NULL);
#else
	WaitForSingleObject (http_thread, INFINITE);
	CloseHandle (http_thread);
#endif
	if (INVALID_SOCKET != http_sock) {
		closesocket (http_sock);
		http_sock = INVALID_SOCKET;
	}
	pgm_notify_destroy (&http_notify);
	return TRUE;
}

/* accept a new incoming HTTP connection.
 */

static
void
http_accept (
	SOCKET		listen_sock
	)
{
/* new connection */
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	SOCKET new_sock = accept (listen_sock, (struct sockaddr*)&addr, &addrlen);
	if (INVALID_SOCKET == new_sock) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		if (PGM_SOCK_EAGAIN == save_errno)
			return;
		pgm_warn (_("HTTP accept: %s"),
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		return;
	}

#ifndef _WIN32
/* out of bounds file descriptor for select() */
	if (new_sock >= FD_SETSIZE) {
		closesocket (new_sock);
		pgm_warn (_("Rejected new HTTP client socket due to out of bounds file descriptor."));
		return;
	}
#endif

	pgm_sockaddr_nonblocking (new_sock, TRUE);

	struct http_connection_t* connection = pgm_new0 (struct http_connection_t, 1);
	connection->sock = new_sock;
	connection->state = HTTP_STATE_READ;
	http_socks = pgm_list_prepend_link (http_socks, &connection->link_);
	FD_SET( new_sock, &http_readfds );
	FD_SET( new_sock, &http_exceptfds );
	if (new_sock > http_max_sock)
		http_max_sock = new_sock;
}

static
void
http_close (
	struct http_connection_t*	connection
	)
{
	if (SOCKET_ERROR == closesocket (connection->sock)) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_warn (_("Close HTTP client socket: %s"),
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
	}
	switch (connection->state) {
	case HTTP_STATE_READ:
	case HTTP_STATE_FINWAIT:
		FD_CLR( connection->sock, &http_readfds );
		break;
	case HTTP_STATE_WRITE:
		FD_CLR( connection->sock, &http_writefds );
		break;
	}
	FD_CLR( connection->sock, &http_exceptfds );
	http_socks = pgm_list_remove_link (http_socks, &connection->link_);
	if (connection->buflen > 0) {
		pgm_free (connection->buf);
		connection->buf = NULL;
		connection->buflen = 0;
	}
/* find new highest fd */
	if (connection->sock == http_max_sock)
	{
		http_max_sock = INVALID_SOCKET;
		for (pgm_list_t* list = http_socks; list; list = list->next)
		{
			struct http_connection_t* c = (void*)list;
			if (c->sock > http_max_sock)
				http_max_sock = c->sock;
		}
	}
	pgm_free (connection);
}

/* non-blocking read an incoming HTTP request
 */

static
void
http_read (
	struct http_connection_t*	connection
	)
{
	for (;;)
	{
/* grow buffer as needed */
		if (connection->bufoff + 1024  > connection->buflen) {
			connection->buf = pgm_realloc (connection->buf, connection->buflen + 1024);
			connection->buflen += 1024;
		}
		const ssize_t bytes_read = recv (connection->sock, &connection->buf[ connection->bufoff ], connection->buflen - connection->bufoff, 0);
		if (bytes_read < 0) {
			const int save_errno = pgm_get_last_sock_error();
			char errbuf[1024];
			if (PGM_SOCK_EINTR == save_errno || PGM_SOCK_EAGAIN == save_errno)
				return;
			pgm_warn (_("HTTP client read: %s"),
				pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
			http_close (connection);
			return;
		}

/* complete */
		if (strstr (connection->buf, "\r\n\r\n"))
			break;
	}

/* process request, e.g. GET /index.html HTTP/1.1\r\n
 */
	connection->buf[ connection->buflen - 1 ] = '\0';
	if (0 != memcmp (connection->buf, "GET ", strlen("GET "))) {
/* 501 (not implemented) */
		http_close (connection);
		return;
	}

	char* request_uri = connection->buf + strlen("GET ");
	char* p = request_uri;
	do {
		if (*p == '?' || *p == ' ') {
			*p = '\0';
			break;
		}
	} while (*(++p));

	connection->status_code	 = 200;	/* OK */
	connection->status_text  = "OK";
	connection->content_type = "text/html";
	connection->bufoff       = 0;
	for (unsigned i = 0; i < PGM_N_ELEMENTS(http_directory); i++)
	{
		if (0 == strcmp (request_uri, http_directory[i].path))
		{
			http_directory[i].callback (connection, request_uri);
			goto complete;
		}
	}
	default_callback (connection, request_uri);

complete:
	connection->state = HTTP_STATE_WRITE;
	FD_CLR( connection->sock, &http_readfds );
	FD_SET( connection->sock, &http_writefds );
}

/* non-blocking write a HTTP response
 */

static
void
http_write (
	struct http_connection_t*	connection
	)
{
	do {
		const ssize_t bytes_written = send (connection->sock, &connection->buf[ connection->bufoff ], connection->buflen - connection->bufoff, 0);
		if (bytes_written < 0) {
			const int save_errno = pgm_get_last_sock_error();
			char errbuf[1024];
			if (PGM_SOCK_EINTR == save_errno || PGM_SOCK_EAGAIN == save_errno)
				return;
			pgm_warn (_("HTTP client write: %s"),
				pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
			http_close (connection);
			return;
		}
		connection->bufoff += bytes_written;
	} while (connection->bufoff < connection->buflen);

	if (0 == shutdown (connection->sock, SHUT_WR)) {
		http_close (connection);
	} else {
		pgm_debug ("HTTP socket entering finwait state.");
		connection->state = HTTP_STATE_FINWAIT;
		FD_CLR( connection->sock, &http_writefds );
		FD_SET( connection->sock, &http_readfds );
	}
}

/* read and discard pending data waiting for FIN
 */

static
void
http_finwait (
	struct http_connection_t*	connection
	)
{
	char buf[1024];
	const ssize_t bytes_read = recv (connection->sock, buf, sizeof(buf), 0);
	if (bytes_read < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_SOCK_EINTR == save_errno || PGM_SOCK_EAGAIN == save_errno)
			return;
	}
	http_close (connection);
}

static
void
http_process (
	struct http_connection_t*	connection
	)
{
	switch (connection->state) {
	case HTTP_STATE_READ:		http_read (connection); break;
	case HTTP_STATE_WRITE:		http_write (connection); break;
	case HTTP_STATE_FINWAIT:	http_finwait (connection); break;
	}
}

static
void
http_set_status (
	struct http_connection_t*restrict connection,
	int				  status_code,
	const char*		 restrict status_text
	)
{
	connection->status_code = status_code;
	connection->status_text = status_text;
}

static
void
http_set_content_type (
	struct http_connection_t*restrict connection,
	const char*		 restrict content_type
	)
{
	connection->content_type = content_type;
}

/* finalise response buffer with headers and content */

static
void
http_set_static_response (
	struct http_connection_t*restrict connection,
	const char*		 restrict content,
	size_t				  content_length
	)
{
	pgm_string_t* response = pgm_string_new (NULL);
	pgm_string_printf (response, "HTTP/1.0 %d %s\r\n"
				     "Server: OpenPGM HTTP Server %u.%u.%u\r\n"
			   	     "Last-Modified: Fri, 1 Jan 2010, 00:00:01 GMT\r\n"
				     "Content-Length: %" PRIzd "\r\n"
				     "Content-Type: %s\r\n"
				     "Connection: close\r\n"
				     "\r\n",
			   connection->status_code,
			   connection->status_text,
			   pgm_major_version, pgm_minor_version, pgm_micro_version,
			   content_length,
			   connection->content_type
			);
	pgm_string_append (response, content);
	if (connection->buflen)
		pgm_free (connection->buf);
	connection->buflen = response->len;
	connection->buf = pgm_string_free (response, FALSE);
}

static
void
http_set_response (
	struct http_connection_t*restrict connection,
	char*			 restrict content,
	size_t				  content_length
	)
{
	pgm_string_t* response = pgm_string_new (NULL);
	pgm_string_printf (response, "HTTP/1.0 %d %s\r\n"
				     "Server: OpenPGM HTTP Server %u.%u.%u\r\n"
				     "Content-Length: %" PRIzd "\r\n"
				     "Content-Type: %s\r\n"
				     "Connection: close\r\n"
				     "\r\n",
			   connection->status_code,
			   connection->status_text,
			   pgm_major_version, pgm_minor_version, pgm_micro_version,
			   content_length,
			   connection->content_type
			);
	pgm_string_append (response, content);
	pgm_free (content);
	if (connection->buflen)
		pgm_free (connection->buf);
	connection->buflen = response->len;
	connection->buf = pgm_string_free (response, FALSE);
}

/* Thread routine for processing HTTP requests
 */

static
#ifndef _WIN32
void*
#else
unsigned
__stdcall
#endif
http_routine (
	PGM_GNUC_UNUSED	void*	arg
	)
{
	const SOCKET notify_fd = pgm_notify_get_socket (&http_notify);
	const int max_fd = MAX( notify_fd, http_sock );

	FD_ZERO( &http_readfds );
	FD_ZERO( &http_writefds );
	FD_ZERO( &http_exceptfds );
	FD_SET( notify_fd, &http_readfds );
	FD_SET( http_sock, &http_readfds );

	for (;;)
	{
		int fds = MAX( http_max_sock, max_fd ) + 1;
		fd_set readfds = http_readfds, writefds = http_writefds, exceptfds = http_exceptfds;

		fds = select (fds, &readfds, &writefds, &exceptfds, NULL);
/* signal interrupt */
		if (PGM_UNLIKELY(SOCKET_ERROR == fds && PGM_SOCK_EINTR == pgm_get_last_sock_error()))
			continue;
/* terminate */
		if (PGM_UNLIKELY(FD_ISSET( notify_fd, &readfds )))
			break;
/* new connection */
		if (FD_ISSET( http_sock, &readfds )) {
			http_accept (http_sock);
			continue;
		}
/* existing connection */
		for (pgm_list_t* list = http_socks; list;)
		{
			struct http_connection_t* c = (void*)list;
			list = list->next;
			if ((FD_ISSET( c->sock, &readfds )  && HTTP_STATE_READ  == c->state) ||
			    (FD_ISSET( c->sock, &writefds ) && HTTP_STATE_WRITE == c->state) ||
			    (FD_ISSET( c->sock, &exceptfds )))
			{
				http_process (c);
			}
		}
	}

/* cleanup */
#ifndef _WIN32
	return NULL;
#else
	_endthread();
	return 0;
#endif /* WIN32 */
}

/* add xhtml doctype and head, populate with runtime values
 */

typedef enum {
	HTTP_TAB_GENERAL_INFORMATION,
	HTTP_TAB_INTERFACES,
	HTTP_TAB_TRANSPORTS,
	HTTP_TAB_HISTOGRAMS
} http_tab_e;

static
pgm_string_t*
http_create_response (
	const char*		subtitle,
	http_tab_e		tab
	)
{
	pgm_assert (NULL != subtitle);
	pgm_assert (tab == HTTP_TAB_GENERAL_INFORMATION ||
		    tab == HTTP_TAB_INTERFACES ||
		    tab == HTTP_TAB_TRANSPORTS ||
		    tab == HTTP_TAB_HISTOGRAMS);

/* surprising deficiency of GLib is no support of display locale time */
	char timestamp[100];
	time_t now;
	time (&now);
	const struct tm* time_ptr = localtime (&now);
#ifndef _WIN32
	strftime (timestamp, sizeof(timestamp), "%c", time_ptr);
#else
	wchar_t wtimestamp[100];
	const size_t slen  = strftime (timestamp, sizeof(timestamp), "%c", time_ptr);
	const size_t wslen = MultiByteToWideChar (CP_ACP, 0, timestamp, slen, wtimestamp, 100);
	WideCharToMultiByte (CP_UTF8, 0, wtimestamp, wslen + 1, timestamp, sizeof(timestamp), NULL, NULL);
#endif

	pgm_string_t* response = pgm_string_new (WWW_XHTML10_STRICT_DOCTYPE);
	pgm_string_append_printf (response, "\n<head>"
						"<title>%s - %s</title>"
						"<link rel=\"stylesheet\" href=\"/base.css\" type=\"text/css\" charset=\"utf-8\" />"
					"</head>\n"
					"<body>"
					"<div id=\"header\">"
						"<span id=\"hostname\">%s</span>"
						" | <span id=\"banner\"><a href=\"http://code.google.com/p/openpgm/\">OpenPGM</a> %u.%u.%u</span>"
						" | <span id=\"timestamp\">%s</span>"
					"</div>"
					"<div id=\"navigation\">"
						"<a href=\"/\"><span class=\"tab\" id=\"tab%s\">General Information</span></a>"
						"<a href=\"/interfaces\"><span class=\"tab\" id=\"tab%s\">Interfaces</span></a>"
						"<a href=\"/transports\"><span class=\"tab\" id=\"tab%s\">Transports</span></a>"
#ifdef USE_HISTOGRAMS
						"<a href=\"/histograms\"><span class=\"tab\" id=\"tab%s\">Histograms</span></a>"
#endif
						"<div id=\"tabline\"></div>"
					"</div>"
					"<div id=\"content\">",
				http_hostname,
				subtitle,
				http_hostname,
				pgm_major_version, pgm_minor_version, pgm_micro_version,
				timestamp,
				tab == HTTP_TAB_GENERAL_INFORMATION ? "top" : "bottom",
				tab == HTTP_TAB_INTERFACES ? "top" : "bottom",
				tab == HTTP_TAB_TRANSPORTS ? "top" : "bottom"
#ifdef USE_HISTOGRAMS
			       ,tab == HTTP_TAB_HISTOGRAMS ? "top" : "bottom"
#endif
	);

	return response;
}

static
void
http_finalize_response (
	struct http_connection_t*restrict connection,
	pgm_string_t*		 restrict response
	)
{
	pgm_string_append (response,	"</div>"
					"<div id=\"footer\">"
						"&copy;2010 Miru"
					"</div>"
					"</body>\n"
					"</html>");

	char* buf = pgm_string_free (response, FALSE);
	http_set_response (connection, buf, strlen (buf));
}

static
void
robots_callback (
	struct http_connection_t*restrict connection,
	PGM_GNUC_UNUSED const char*restrict path
        )
{
	http_set_content_type (connection, "text/plain");
        http_set_static_response (connection, WWW_ROBOTS_TXT, strlen(WWW_ROBOTS_TXT));
}

static
void
css_callback (
	struct http_connection_t*restrict connection,
	PGM_GNUC_UNUSED const char*restrict path
        )
{       
	http_set_content_type (connection, "text/css");
        http_set_static_response (connection, WWW_BASE_CSS, strlen(WWW_BASE_CSS));
}

static
void
index_callback (
	struct http_connection_t*restrict connection,
	const char*		 restrict path
        )
{
	if (strlen (path) > 1) {
		default_callback (connection, path);
		return;
	}
	pgm_rwlock_reader_lock (&pgm_sock_list_lock);
	const unsigned transport_count = pgm_slist_length (pgm_sock_list);
	pgm_rwlock_reader_unlock (&pgm_sock_list_lock);

	pgm_string_t* response = http_create_response ("OpenPGM", HTTP_TAB_GENERAL_INFORMATION);
	pgm_string_append_printf (response,	"<table>"
						"<tr>"
							"<th>host name:</th><td>%s</td>"
						"</tr><tr>"
							"<th>user name:</th><td>%s</td>"
						"</tr><tr>"
							"<th>IP address:</th><td>%s</td>"
						"</tr><tr>"
							"<th>transports:</th><td><a href=\"/transports\">%i</a></td>"
						"</tr><tr>"
							"<th>process ID:</th><td>%i</td>"
						"</tr>"
						"</table>\n",
				http_hostname,
				http_username,
				http_address,
				transport_count,
				http_pid);
	http_finalize_response (connection, response);
}

static
void
interfaces_callback (
	struct http_connection_t*restrict connection,
	PGM_GNUC_UNUSED const char*restrict path
        )
{
	pgm_string_t* response = http_create_response ("Interfaces", HTTP_TAB_INTERFACES);
	pgm_string_append (response, "<PRE>");
	struct pgm_ifaddrs_t *ifap, *ifa;
	pgm_error_t* err = NULL;
	if (!pgm_getifaddrs (&ifap, &err)) {
		pgm_string_append_printf (response, "pgm_getifaddrs(): %s", (err && err->message) ? err->message : "(null)");
		http_finalize_response (connection, response);
		return;
	}
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		unsigned i = NULL == ifa->ifa_addr ? 0 : pgm_if_nametoindex (ifa->ifa_addr->sa_family, ifa->ifa_name);
		char rname[IF_NAMESIZE * 2 + 3];
		char b[IF_NAMESIZE * 2 + 3];

		pgm_if_indextoname (i, rname);
		sprintf (b, "%s (%s)", ifa->ifa_name, rname);

		 if (NULL == ifa->ifa_addr ||
		      (ifa->ifa_addr->sa_family != AF_INET &&
		       ifa->ifa_addr->sa_family != AF_INET6) )
		{
			pgm_string_append_printf (response,
				"#%d name %-15.15s ---- %-46.46s scope 0 status %s loop %s b/c %s m/c %s<BR/>\n",
				i,
				b,
				"",
				ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
				ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
				ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
				ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
			continue;
		}

		char s[INET6_ADDRSTRLEN];
		getnameinfo (ifa->ifa_addr, pgm_sockaddr_len(ifa->ifa_addr),
			     s, sizeof(s),
			     NULL, 0,
			     NI_NUMERICHOST);
		pgm_string_append_printf (response,
			"#%d name %-15.15s IPv%i %-46.46s scope %u status %s loop %s b/c %s m/c %s<BR/>\n",
			i,
			b,
			ifa->ifa_addr->sa_family == AF_INET ? 4 : 6,
			s,
			(unsigned)pgm_sockaddr_scope_id(ifa->ifa_addr),
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
		);
	}
	pgm_freeifaddrs (ifap);
	pgm_string_append (response, "</PRE>\n");
	http_finalize_response (connection, response);
}

static
void
transports_callback (
	struct http_connection_t*restrict connection,
	PGM_GNUC_UNUSED const char*restrict path
        )
{
	pgm_string_t* response = http_create_response ("Transports", HTTP_TAB_TRANSPORTS);
	pgm_string_append (response,	"<div class=\"bubbly\">"
					"\n<table cellspacing=\"0\">"
					"<tr>"
						"<th>Group address</th>"
						"<th>Dest port</th>"
						"<th>Source GSI</th>"
						"<th>Source port</th>"
					"</tr>"
				);

	if (pgm_sock_list)
	{
		pgm_rwlock_reader_lock (&pgm_sock_list_lock);

		pgm_slist_t* list = pgm_sock_list;
		while (list)
		{
			pgm_slist_t* next = list->next;
			pgm_sock_t*  sock = list->data;

			char group_address[INET6_ADDRSTRLEN];
			getnameinfo ((struct sockaddr*)&sock->send_gsr.gsr_group, pgm_sockaddr_len ((struct sockaddr*)&sock->send_gsr.gsr_group),
				     group_address, sizeof(group_address),
				     NULL, 0,
				     NI_NUMERICHOST);
			char gsi[ PGM_GSISTRLEN ];
			pgm_gsi_print_r (&sock->tsi.gsi, gsi, sizeof(gsi));
			const in_port_t sport = ntohs (sock->tsi.sport);
			const in_port_t dport = ntohs (sock->dport);
			pgm_string_append_printf (response,	"<tr>"
									"<td>%s</td>"
									"<td>%u</td>"
									"<td><a href=\"/%s.%u\">%s</a></td>"
									"<td><a href=\"/%s.%u\">%u</a></td>"
								"</tr>",
						group_address,
						dport,
						gsi, sport,
						gsi,
						gsi, sport,
						sport);
			list = next;
		}
		pgm_rwlock_reader_unlock (&pgm_sock_list_lock);
	}
	else
	{
/* no transports */
		pgm_string_append (response,		"<tr>"
							"<td colspan=\"6\"><div class=\"empty\">This transport has no peers.</div></td>"
							"</tr>"
				);
	}

	pgm_string_append (response,		"</table>\n"
						"</div>");
	http_finalize_response (connection, response);
}

static
void
histograms_callback (
	struct http_connection_t*restrict connection,
	PGM_GNUC_UNUSED const char*restrict path
        )
{
	pgm_string_t* response = http_create_response ("Histograms", HTTP_TAB_HISTOGRAMS);
	pgm_histogram_write_html_graph_all (response);
	http_finalize_response (connection, response);
}

static
void
default_callback (
	struct http_connection_t*restrict connection,
	const char*		 restrict path
        )
{
	pgm_tsi_t tsi;
	const int count = sscanf (path, "/%hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hu",
				(unsigned char*)&tsi.gsi.identifier[0],
				(unsigned char*)&tsi.gsi.identifier[1],
				(unsigned char*)&tsi.gsi.identifier[2],
				(unsigned char*)&tsi.gsi.identifier[3],
				(unsigned char*)&tsi.gsi.identifier[4],
				(unsigned char*)&tsi.gsi.identifier[5],
				&tsi.sport);
	tsi.sport = htons (tsi.sport);
	if (count == 7)
	{
		const int retval = http_tsi_response (connection, &tsi);
		if (!retval) return;
	}

	http_set_status (connection, 404, "Not Found");
	http_set_static_response (connection, WWW_404_HTML, strlen(WWW_404_HTML));
}

static
int
http_tsi_response (
	struct http_connection_t*restrict connection,
	const pgm_tsi_t*	 restrict tsi
	)
{
/* first verify this is a valid TSI */
	pgm_rwlock_reader_lock (&pgm_sock_list_lock);

	pgm_sock_t* sock = NULL;
	pgm_slist_t* list = pgm_sock_list;
	while (list)
	{
		pgm_sock_t* list_sock = (pgm_sock_t*)list->data;
		pgm_slist_t* next = list->next;

/* check source */
		if (pgm_tsi_equal (tsi, &list_sock->tsi))
		{
			sock = list_sock;
			break;
		}

/* check receivers */
		pgm_rwlock_reader_lock (&list_sock->peers_lock);
		pgm_peer_t* receiver = pgm_hashtable_lookup (list_sock->peers_hashtable, tsi);
		if (receiver) {
			const int retval = http_receiver_response (connection, list_sock, receiver);
			pgm_rwlock_reader_unlock (&list_sock->peers_lock);
			pgm_rwlock_reader_unlock (&pgm_sock_list_lock);
			return retval;
		}
		pgm_rwlock_reader_unlock (&list_sock->peers_lock);

		list = next;
	}

	if (!sock) {
		pgm_rwlock_reader_unlock (&pgm_sock_list_lock);
		return -1;
	}

/* transport now contains valid matching TSI */
	char gsi[ PGM_GSISTRLEN ];
	pgm_gsi_print_r (&sock->tsi.gsi, gsi, sizeof(gsi));

	char title[ sizeof("Transport .00000") + PGM_GSISTRLEN ];
	sprintf (title, "Transport %s.%hu",
		 gsi,
		 ntohs (sock->tsi.sport));

	char source_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&sock->send_gsr.gsr_source, pgm_sockaddr_len ((struct sockaddr*)&sock->send_gsr.gsr_source),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&sock->send_gsr.gsr_group, pgm_sockaddr_len ((struct sockaddr*)&sock->send_gsr.gsr_group),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	const in_port_t dport = ntohs (sock->dport);
	const in_port_t sport = ntohs (sock->tsi.sport);

	const pgm_time_t ihb_min = sock->spm_heartbeat_len ? sock->spm_heartbeat_interval[ 1 ] : 0;
	const pgm_time_t ihb_max = sock->spm_heartbeat_len ? sock->spm_heartbeat_interval[ sock->spm_heartbeat_len - 1 ] : 0;

	char spm_path[INET6_ADDRSTRLEN];
	getnameinfo ((struct sockaddr*)&sock->recv_gsr[0].gsr_source, pgm_sockaddr_len ((struct sockaddr*)&sock->recv_gsr[0].gsr_source),
		     spm_path, sizeof(spm_path),
		     NULL, 0,
		     NI_NUMERICHOST);

	pgm_string_t* response = http_create_response (title, HTTP_TAB_TRANSPORTS);
	pgm_string_append_printf (response,	"<div class=\"heading\">"
							"<strong>Transport: </strong>"
							"%s.%u"
						"</div>",
				  gsi, sport);

/* peers */

	pgm_string_append (response,		"<div class=\"bubbly\">"
						"\n<table cellspacing=\"0\">"
						"<tr>"
							"<th>Group address</th>"
							"<th>Dest port</th>"
							"<th>Source address</th>"
							"<th>Last hop</th>"
							"<th>Source GSI</th>"
							"<th>Source port</th>"
						"</tr>"
			);

	if (sock->peers_list)
	{
		pgm_rwlock_reader_lock (&sock->peers_lock);
		pgm_list_t* peers_list = sock->peers_list;
		while (peers_list) {
			pgm_list_t* next = peers_list->next;
			http_each_receiver (sock, peers_list->data, response);
			peers_list = next;
		}
		pgm_rwlock_reader_unlock (&sock->peers_lock);
	}
	else
	{
/* no peers */

		pgm_string_append (response,	"<tr>"
							"<td colspan=\"6\"><div class=\"empty\">This transport has no peers.</div></td>"
						"</tr>"
				);

	}

	pgm_string_append (response,		"</table>\n"
						"</div>");

/* source and configuration information */

	pgm_string_append_printf (response,	"<div class=\"rounded\" id=\"information\">"
						"\n<table>"
						"<tr>"
							"<th>Source address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Group address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Dest port</th><td>%u</td>"
						"</tr><tr>"
							"<th>Source GSI</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source port</th><td>%u</td>"
						"</tr>",
				source_address,
				group_address,
				dport,
				gsi,
				sport);

/* continue with source information */

	pgm_string_append_printf (response,	"<tr>"
							"<td colspan=\"2\"><div class=\"break\"></div></td>"
						"</tr><tr>"
							"<th>Ttl</th><td>%u</td>"
						"</tr><tr>"
							"<th>Adv Mode</th><td>%s</td>"
						"</tr><tr>"
							"<th>Late join</th><td>disable(2)</td>"
						"</tr><tr>"
							"<th>TXW_MAX_RTE</th><td>%" GROUP_FORMAT "zd</td>"
						"</tr><tr>"
							"<th>TXW_SECS</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>TXW_ADV_SECS</th><td>0</td>"
						"</tr><tr>"
							"<th>Ambient SPM interval</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>IHB_MIN</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>IHB_MAX</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_BO_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>FEC</th><td>disabled(1)</td>"
						"</tr><tr>"
							"<th>Source Path Address</th><td>%s</td>"
						"</tr>"
						"</table>\n"
						"</div>",
				sock->hops,
				0 == sock->adv_mode ? "time(0)" : "data(1)",
				sock->txw_max_rte,
				sock->txw_secs,
				pgm_to_msecs(sock->spm_ambient_interval),
				ihb_min,
				ihb_max,
				pgm_to_msecs(sock->nak_bo_ivl),
				spm_path);

/* performance information */

	const pgm_txw_t* window = sock->window;
	pgm_string_append_printf (response,	"\n<h2>Performance information</h2>"
						"\n<table>"
						"<tr>"
							"<th>Data bytes sent</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Data packets sent</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Bytes buffered</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Packets buffered</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Bytes sent</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Raw NAKs received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Checksum errors</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed NAKs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Packets discarded</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Bytes retransmitted</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Packets retransmitted</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs ignored</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Transmission rate</th><td>%" GROUP_FORMAT PRIu32 " bps</td>"
						"</tr><tr>"
							"<th>NNAK packets received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NNAKs received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed NNAKs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr>"
						"</table>\n",
						sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT],
						sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT],
						window ? (uint32_t)pgm_txw_size (window) : 0,	/* minus IP & any UDP header */
						window ? (uint32_t)pgm_txw_length (window) : 0,
						sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED],
						sock->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS],
						sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_IGNORED],
						sock->cumulative_stats[PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED],
						sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED],
						sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]);

	pgm_rwlock_reader_unlock (&pgm_sock_list_lock);
	http_finalize_response (connection, response);
	return 0;
}

static
void
http_each_receiver (
	const pgm_sock_t*	restrict sock,
	const pgm_peer_t*	restrict peer,
	pgm_string_t*		restrict response
	)
{
	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->group_nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->group_nla),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char source_address[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->nla),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char last_hop[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->local_nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->local_nla),
		     last_hop, sizeof(last_hop),
		     NULL, 0,
		     NI_NUMERICHOST);

	char gsi[ PGM_GSISTRLEN + sizeof(".00000") ];
	pgm_gsi_print_r (&peer->tsi.gsi, gsi, sizeof(gsi));

	const uint16_t sport = ntohs (peer->tsi.sport);
	const uint16_t dport = ntohs (sock->dport);	/* by definition must be the same */
	pgm_string_append_printf (response,	"<tr>"
							"<td>%s</td>"
							"<td>%u</td>"
							"<td>%s</td>"
							"<td>%s</td>"
							"<td><a href=\"/%s.%u\">%s</a></td>"
							"<td><a href=\"/%s.%u\">%u</a></td>"
						"</tr>",
				group_address,
				dport,
				source_address,
				last_hop,
				gsi, sport, gsi,
				gsi, sport, sport
			);
}

static
int
http_time_summary (
	const time_t*	restrict activity_time,
	char* 		restrict sz
	)
{
	time_t now_time = time (NULL);

	if (*activity_time > now_time) {
		return sprintf (sz, "clock skew");
	}

	struct tm* activity_tm = localtime (activity_time);

	now_time -= *activity_time;

	if (now_time < (24 * 60 * 60))
	{
		char hourmin[6];
		strftime (hourmin, sizeof(hourmin), "%H:%M", activity_tm);

		if (now_time < 60) {
			return sprintf (sz, "%s (%li second%s ago)",
					hourmin, now_time, now_time > 1 ? "s" : "");
		}
		now_time /= 60;
		if (now_time < 60) {
			return sprintf (sz, "%s (%li minute%s ago)",
					hourmin, now_time, now_time > 1 ? "s" : "");
		}
		now_time /= 60;
		return sprintf (sz, "%s (%li hour%s ago)",
				hourmin, now_time, now_time > 1 ? "s" : "");
	}
	else
	{
		char daymonth[32];
#ifndef _WIN32
		strftime (daymonth, sizeof(daymonth), "%d %b", activity_tm);
#else
		wchar_t wdaymonth[32];
		const size_t slen  = strftime (daymonth, sizeof(daymonth), "%d %b", &activity_tm);
		const size_t wslen = MultiByteToWideChar (CP_ACP, 0, daymonth, slen, wdaymonth, 32);
		WideCharToMultiByte (CP_UTF8, 0, wdaymonth, wslen + 1, daymonth, sizeof(daymonth), NULL, NULL);
#endif
		now_time /= 24;
		if (now_time < 14) {
			return sprintf (sz, "%s (%li day%s ago)",
					daymonth, now_time, now_time > 1 ? "s" : "");
		} else {
			return sprintf (sz, "%s", daymonth);
		}
	}
}

static
int
http_receiver_response (
	struct http_connection_t*restrict connection,
	const pgm_sock_t*	 restrict sock,
	const pgm_peer_t*	 restrict peer
	)
{
	char gsi[ PGM_GSISTRLEN ];
	pgm_gsi_print_r (&peer->tsi.gsi, gsi, sizeof(gsi));
	char title[ sizeof("Peer .00000") + PGM_GSISTRLEN ];
	sprintf (title, "Peer %s.%u",
		 gsi,
		 ntohs (peer->tsi.sport));

	char group_address[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->group_nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->group_nla),
		     group_address, sizeof(group_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char source_address[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->nla),
		     source_address, sizeof(source_address),
		     NULL, 0,
		     NI_NUMERICHOST);

	char last_hop[INET6_ADDRSTRLEN];
	getnameinfo ((const struct sockaddr*)&peer->local_nla, pgm_sockaddr_len ((const struct sockaddr*)&peer->local_nla),
		     last_hop, sizeof(last_hop),
		     NULL, 0,
		     NI_NUMERICHOST);

	const in_port_t sport = ntohs (peer->tsi.sport);
	const in_port_t dport = ntohs (sock->dport);	/* by definition must be the same */
	const pgm_rxw_t* window = peer->window;
	const uint32_t outstanding_naks = window->nak_backoff_queue.length +
					  window->wait_ncf_queue.length +
					  window->wait_data_queue.length;

	time_t last_activity_time;
	pgm_time_since_epoch (&peer->last_packet, &last_activity_time);

	char last_activity[100];
	http_time_summary (&last_activity_time, last_activity);

	pgm_string_t* response = http_create_response (title, HTTP_TAB_TRANSPORTS);
	pgm_string_append_printf (response,	"<div class=\"heading\">"
							"<strong>Peer: </strong>"
							"%s.%u"
						"</div>",
				  gsi, sport);


/* peer information */
	pgm_string_append_printf (response,	"<div class=\"rounded\" id=\"information\">"
						"\n<table>"
						"<tr>"
							"<th>Group address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Dest port</th><td>%u</td>"
						"</tr><tr>"
							"<th>Source address</th><td>%s</td>"
						"</tr><tr>"
							"<th>Last hop</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source GSI</th><td>%s</td>"
						"</tr><tr>"
							"<th>Source port</th><td>%u</td>"
						"</tr>",
				group_address,
				dport,
				source_address,
				last_hop,
				gsi,
				sport);

	pgm_string_append_printf (response,	"<tr>"
							"<td colspan=\"2\"><div class=\"break\"></div></td>"
						"</tr><tr>"
							"<th>NAK_BO_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_RPT_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_NCF_RETRIES</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>NAK_RDATA_IVL</th><td>%" GROUP_FORMAT PGM_TIME_FORMAT " ms</td>"
						"</tr><tr>"
							"<th>NAK_DATA_RETRIES</th><td>%" GROUP_FORMAT "u</td>"
						"</tr><tr>"
							"<th>Send NAKs</th><td>enabled(1)</td>"
						"</tr><tr>"
							"<th>Late join</th><td>disabled(2)</td>"
						"</tr><tr>"
							"<th>NAK TTL</th><td>%u</td>"
						"</tr><tr>"
							"<th>Delivery order</th><td>ordered(2)</td>"
						"</tr><tr>"
							"<th>Multicast NAKs</th><td>disabled(2)</td>"
						"</tr>"
						"</table>\n"
						"</div>",
						pgm_to_msecs (sock->nak_bo_ivl),
						pgm_to_msecs (sock->nak_rpt_ivl),
						sock->nak_ncf_retries,
						pgm_to_msecs (sock->nak_rdata_ivl),
						sock->nak_data_retries,
						sock->hops);

	pgm_string_append_printf (response,	"\n<h2>Performance information</h2>"
						"\n<table>"
						"<tr>"
							"<th>Data bytes received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Data packets received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAK failures</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Bytes received</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Checksum errors</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed SPMs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed ODATA</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed RDATA</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed NCFs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Packets discarded</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Losses</th><td>%" GROUP_FORMAT PRIu32 "</td>"	/* detected missed packets */
						"</tr><tr>"
							"<th>Bytes delivered to app</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Packets delivered to app</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Duplicate SPMs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Duplicate ODATA/RDATA</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAK packets sent</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs sent</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs retransmitted</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs failed</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to RXW advance</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to NCF retries</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs failed due to DATA retries</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAK failures delivered to app</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAKs suppressed</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Malformed NAKs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Outstanding NAKs</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>Last activity</th><td>%s</td>"
						"</tr><tr>"
							"<th>NAK repair min time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK repair mean time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK repair max time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK fail min time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK fail mean time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK fail max time</th><td>%" GROUP_FORMAT PRIu32 " μs</td>"
						"</tr><tr>"
							"<th>NAK min retransmit count</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAK mean retransmit count</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr><tr>"
							"<th>NAK max retransmit count</th><td>%" GROUP_FORMAT PRIu32 "</td>"
						"</tr>"
						"</table>\n",
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAILURES],
						peer->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED],
						sock->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_RDATA],
						peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS],
						peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED],
						window->cumulative_losses,
						window->bytes_delivered,
						window->msgs_delivered,
						peer->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS],
						peer->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_RETRANSMITTED],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_FAILED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_RXW_ADVANCED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAILURES_DELIVERED],
						peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED],
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS],
						outstanding_naks,
						last_activity,
						window->min_fill_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_SVC_TIME_MEAN],
						window->max_fill_time,
						peer->min_fail_time,
						peer->cumulative_stats[PGM_PC_RECEIVER_NAK_FAIL_TIME_MEAN],
						peer->max_fail_time,
						window->min_nak_transmit_count,
						peer->cumulative_stats[PGM_PC_RECEIVER_TRANSMIT_MEAN],
						window->max_nak_transmit_count);
	http_finalize_response (connection, response);
	return 0;
}

/* eof */

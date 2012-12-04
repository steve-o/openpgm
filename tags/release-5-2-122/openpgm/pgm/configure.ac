# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#  Ubuntu 10 : v2.65
#   OSX 10.6 : v2.61
# Solaris 10 : v2.59
AC_PREREQ([2.61])
AC_INIT([OpenPGM], [m4_esyscmd([perl version.pl %major.%minor.%micro])], [openpgm-dev@googlegroups.com], [openpgm], [http://code.google.com/p/openpgm/])
AC_CONFIG_SRCDIR([reed_solomon.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER(include/config.h)

#  Ubuntu 10 : v1.11
#   OSX 10.6 : v1.10
# Solaris 10 : v1.8
AM_INIT_AUTOMAKE([1.10 no-define foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_SUBST([RELEASE_INFO], [m4_esyscmd([perl version.pl %major.%minor])])
AC_SUBST([VERSION_INFO], [m4_esyscmd([perl version.pl 0:%micro])])

AC_SUBST([VERSION_MAJOR], [m4_esyscmd([perl version.pl %major])])
AC_SUBST([VERSION_MINOR], [m4_esyscmd([perl version.pl %minor])])
AC_SUBST([VERSION_MICRO], [m4_esyscmd([perl version.pl %micro])])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_LIBTOOL
AC_PATH_PROG(PERL, perl)
AC_PATH_PROG(PYTHON, python)

# nb: earliest verifiable version is 2.2.
m4_ifdef([LT_PREREQ], [LT_PREREQ([2.2])])
m4_ifdef([LT_INIT], [LT_INIT])
AC_SUBST([LIBTOOL_DEPS])
AC_SUBST(PERL)
AC_SUBST(PYTHON)

# Apply system specific rules.
AC_CANONICAL_HOST
CFLAGS="$CFLAGS -D_REENTRANT"
case "$host_os" in
linux*)
	CFLAGS="$CFLAGS -D_XOPEN_SOURCE=600 -D_BSD_SOURCE"
	;;
solaris*)
	CFLAGS="$CFLAGS -D_XOPEN_SOURCE=600 -D__EXTENSIONS__"
	AC_SEARCH_LIBS([socket], [socket])
	AC_SEARCH_LIBS([gethostname], [nsl])
	AC_SEARCH_LIBS([inet_aton], [resolv])
	AC_SEARCH_LIBS([kstat_open], [kstat])
	;;
*)
	;;
esac

# Checks for libraries.
AC_SEARCH_LIBS([sqrt], [m])
AC_SEARCH_LIBS([pthread_mutex_trylock], [pthread])
AC_SEARCH_LIBS([clock_gettime], [rt])

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h libintl.h limits.h locale.h malloc.h memory.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h sys/timeb.h syslog.h unistd.h wchar.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_C_FLEXIBLE_ARRAY_MEMBER
AC_C_RESTRICT
AC_C_VARARRAYS
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
# TODO: gettext() fails out-of-the-box from AutoConf.
#AM_GNU_GETTEXT
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([atexit clock_gettime floor ftime gethostbyaddr gethostbyname gethostname gettimeofday inet_ntoa memmove memset regcomp select setenv setlocale socket sqrt stpcpy strcasecmp strchr strdup strerror strncasecmp strpbrk strrchr strstr strtol strtoul strtoull])

# POSIX spinlocks
AC_MSG_CHECKING([for pthread_spinlock])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <pthread.h>]],
		[[pthread_spinlock_t spinlock; pthread_spin_lock (&spinlock);]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_PTHREAD_SPINLOCK"],
	[AC_MSG_RESULT([no])])
# NSS protocol lookup
AC_CHECK_FUNCS([getprotobyname_r])
if test "x$ac_cv_func_getprotobyname_r" = "xyes"; then
AC_MSG_CHECKING([whether getprotobyname_r returns struct protoent *])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <netdb.h>]],
		[[struct protoent *pe = getprotobyname_r ((const char*)0, (struct protoent*)0, (char*)0, (int)0);]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DGETPROTOBYNAME_R_STRUCT_PROTOENT_P"],
	[AC_MSG_RESULT([no])])
fi
# NSS networks lookup, IPv4 only
AC_CHECK_FUNCS([getnetent])
# variadic macros
AC_MSG_CHECKING([for C99 variadic macros])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <stdio.h>
#define error(...) fprintf (stderr, __VA_ARGS__)]],
		[[error("moo");]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_ISO_VARARGS"],
	[AC_MSG_RESULT([no])])
AC_MSG_CHECKING([for GNU-style variadic macros])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <stdio.h>
#define error(x...) fprintf (stderr, x)]],
		[[error("moo");]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_GNUC_VARARGS"],
	[AC_MSG_RESULT([no])])
# stack memory api header
AC_FUNC_ALLOCA
# eventfd API
AC_MSG_CHECKING([for eventfd])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <sys/eventfd.h>]],
                [[eventfd (0, 0);]])],
        [AC_MSG_RESULT([yes])
                CFLAGS="$CFLAGS -DHAVE_EVENTFD"],
        [AC_MSG_RESULT([no])])
# useful /proc system
AC_CHECK_FILES([/proc/cpuinfo])
# example: crash handling
AC_CHECK_FUNCS([backtrace])
# timing
AC_CHECK_FUNCS([pselect])
AC_CHECK_FILES([/dev/rtc])
AC_MSG_CHECKING([for RDTSC instruction])
case "$host_os" in
darwin*)
	AC_MSG_RESULT([no])
	;;
*)
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(,[[unsigned long lo, hi;
__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_RDTSC"],
	[AC_MSG_RESULT([no])])
	;;
esac
AC_CHECK_FILES([/dev/hpet])
# event handling
AC_CHECK_FUNCS([poll])
AC_CHECK_FUNCS([epoll_ctl])
# interface enumeration
AC_CHECK_FUNCS([getifaddrs])
AC_MSG_CHECKING([for struct ifreq.ifr_netmask])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <sys/types.h>
#include <ifaddrs.h>]],
		[[struct ifreq ifr;
ifr.ifr_netmask = (struct sockaddr*)0;]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_STRUCT_IFREQ_IFR_NETMASK"],
	[AC_MSG_RESULT([no])])
# win32 cmsg
AC_CHECK_MEMBER([struct _WSAMSG.name],
		CFLAGS="$CFLAGS -DHAVE_WSACMSGHDR",)
# multicast
AC_MSG_CHECKING([for struct group_req.gr_interface])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <netinet/in.h>]],
		[[struct group_req gr;
gr.gr_interface = 0;]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_STRUCT_GROUP_REQ"],
	[AC_MSG_RESULT([no])])
AC_MSG_CHECKING([for struct ip_mreqn.imr_ifindex])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <netinet/in.h>]],
		[[struct ip_mreqn mreqn;
mreqn.imr_ifindex = 0;]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_STRUCT_IP_MREQN"],
	[AC_MSG_RESULT([no])])
AC_MSG_CHECKING([for struct ip_msfilter.imsf_multiaddr])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <sys/sockio.h>]],
		[[struct ip_msfilter msf;
msf.imsf_numsrc = 0;]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_STRUCT_IP_MSFILTER"],
	[AC_MSG_RESULT([no])])
# sprintf, caveat http://savannah.gnu.org/patch/?6848 (ax_c_printf_thsep)
AC_MSG_CHECKING([for printf thousands' grouping])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(,[[printf ("%'d", 1000000);]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS -DHAVE_SPRINTF_GROUPING"],
	[AC_MSG_RESULT([no])])
AC_CHECK_FUNCS([vasprintf])
# symbol linking scope
# nb: sun x86 ld doesn't support DSO visibility but the compiler raises
# warnings and these are easier to detect in autoconf.
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_MSG_CHECKING([for hidden visibility attribute])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#ifdef __SUNPRO_C
__hidden
#else
__attribute__((visibility("hidden")))
#endif
void moo (void) {};]],
		[[moo();]])],
	[AC_MSG_RESULT([yes])
		CFLAGS="$save_CFLAGS -DHAVE_DSO_VISIBILITY"],
	[AC_MSG_RESULT([no])
		CFLAGS="$save_CFLAGS"])

# socket binding
CFLAGS="$CFLAGS -DUSE_BIND_INADDR_ANY"
# IP header order as per IP(4) on FreeBSD
AC_MSG_CHECKING([for raw IP sockets ip_{len,off} host byte ordering])
case "$host_os" in
*openbsd*)
	AC_MSG_RESULT([no])
	;;
*bsd*|*darwin*|*osf*|*unixware*)
	AC_MSG_RESULT([yes])
	CFLAGS="$CFLAGS -DHAVE_HOST_ORDER_IP_LEN -DHAVE_HOST_ORDER_IP_OFF"
	;;
*)
	AC_MSG_RESULT([no])
	;;
esac
# RFC 3542 based IPv6 socket options which are incompatible with RFC 2292
case "$host_os" in
*darwin*)
	AC_MSG_CHECKING([for IPV6_PKTINFO])
	CFLAGS="-D__APPLE_USE_RFC_3542"
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <netinet/in.h>]],[[int ipopt = IPV6_PKTINFO;]])],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no])])
	;;
esac
# extended assembler on SPARC
case "$host" in
sparc-sun-solaris*)
	AC_MSG_CHECKING([for SPARC extended assembler])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <stdint.h>
uint32_t add32_with_carry (uint32_t a, uint32_t b) {
        __asm__ ( "addcc %2, %0, %0\n\taddx %0, %%g0, %0" : "=r" (a) : "0" (a), "r" (b) : "cc");
        return a;
}]],
			[[uint32_t c = add32_with_carry (1, 2);]])],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([optimization required])
			CFLAGS="$CFLAGS -xO1"])
	;;
*)
	;;
esac
# ticket spinlock friendly: unaligned pointers & atomic ops (excl. Sun Pro)
AC_MSG_CHECKING([for unaligned pointers])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[char* nezumi = "mouse";]],
		[[short x = *(short*)(nezumi + 2)]])],
	[AC_MSG_RESULT([yes])
		pgm_unaligned_pointers=yes],
	[AC_MSG_RESULT([no])
		pgm_unaligned_pointers=no])
AC_MSG_CHECKING([for intrinsic atomic ops])
# AC_PREPROC_IFELSE not always portable
AC_COMPILE_IFELSE(
	[AC_LANG_SOURCE([[#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
/* GCC assembler */
#elif defined( __sun )
/* Solaris intrinsic */
#elif defined( __APPLE__ )
/* Darwin intrinsic */
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* GCC 4.0.1 intrinsic */
#elif defined( _WIN32 )
/* Windows intrinsic */
#else
#	error "Unsupported atomic ops."
#endif]])],
	[AC_MSG_RESULT([yes])
		if test "$pgm_unaligned_pointers" = yes; then
			CFLAGS="$CFLAGS -DUSE_TICKET_SPINLOCK -DUSE_DUMB_RWSPINLOCK"
		else
			CFLAGS="$CFLAGS -DUSE_TICKET_SPINLOCK"
		fi],
	[AC_MSG_RESULT([no])])

AC_CONFIG_FILES([Makefile openpgm-${RELEASE_INFO}.pc openpgm.spec])
AC_OUTPUT

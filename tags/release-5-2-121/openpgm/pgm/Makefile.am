# -*- Makefile -*-

ACLOCAL_AMFLAGS = -I m4

noinst_LTLIBRARIES = libpgm_noinst.la

EXTRA_DIST = galois_generator.pl version_generator.py

libpgm_noinst_la_SOURCES = \
	thread.c \
	mem.c \
	string.c \
	list.c \
	slist.c \
	queue.c \
	hashtable.c \
	messages.c \
	error.c \
	math.c \
	packet_parse.c \
	packet_test.c \
	sockaddr.c \
	time.c \
	if.c \
	inet_lnaof.c \
	getifaddrs.c \
	get_nprocs.c \
	getnetbyname.c \
	getnodeaddr.c \
	getprotobyname.c \
	indextoaddr.c \
	indextoname.c \
	nametoindex.c \
	inet_network.c \
	md5.c \
	rand.c \
	gsi.c \
	tsi.c \
	txw.c \
	rxw.c \
	skbuff.c \
	socket.c \
	source.c \
	receiver.c \
	recv.c \
	engine.c \
	timer.c \
	net.c \
	rate_control.c \
	checksum.c \
	reed_solomon.c \
	galois_tables.c \
	wsastrerror.c \
	histogram.c \
	version.c

share_includedir = $(includedir)/pgm-@RELEASE_INFO@/pgm
share_include_HEADERS = \
	include/pgm/atomic.h \
	include/pgm/engine.h \
	include/pgm/error.h \
	include/pgm/gsi.h \
	include/pgm/if.h \
	include/pgm/in.h \
	include/pgm/list.h \
	include/pgm/macros.h \
	include/pgm/mem.h \
	include/pgm/messages.h \
	include/pgm/msgv.h \
	include/pgm/packet.h \
	include/pgm/pgm.h \
	include/pgm/skbuff.h \
	include/pgm/socket.h \
	include/pgm/time.h \
	include/pgm/tsi.h \
	include/pgm/types.h \
	include/pgm/version.h \
	include/pgm/winint.h \
	include/pgm/wininttypes.h \
	include/pgm/zinttypes.h

# nb: Solaris make does not understand $<
version.c: version_generator.py
	$(AM_V_GEN)$(PYTHON) $(srcdir)/version_generator.py > $@

galois_tables.c: galois_generator.pl
	$(AM_V_GEN)$(PERL) $(srcdir)/galois_generator.pl > $@

libpgm_noinst_la_CFLAGS = \
	-I$(top_srcdir)/include \
	-DUSE_16BIT_CHECKSUM \
	-DUSE_GALOIS_MUL_LUT \
	-DGETTEXT_PACKAGE='"pgm"'

lib_LTLIBRARIES   = libpgm.la
libpgm_la_SOURCES =
libpgm_la_CFLAGS  = $(libpgm_noinst_la_CFLAGS)
libpgm_la_LDFLAGS = -release @RELEASE_INFO@ -version-info @VERSION_INFO@
libpgm_la_LIBADD  = libpgm_noinst.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openpgm-@RELEASE_INFO@.pc

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status libtool

dist-hook:
	-cp $(top_srcdir)/openpgm.spec $(distdir)/openpgm.spec
	-rm -rf $(distdir)/include
	-cp -R $(top_srcdir)/include $(distdir)/include
	-find $(distdir) -type d -name .svn -exec rm -r {} \; >/dev/null 2>&1 || true

# eof

# -*- Makefile -*-

lib_LTLIBRARIES = libpgm.la

libpgm_la_SOURCES = \
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
	getifaddrs.c \
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

version.c: version_generator.py
	$(PYTHON) $< > $@

galois_tables.c: galois_generator.pl
	$(PERL) $< > $@

libpgm_la_CFLAGS = \
	-I$(top_srcdir)/include \
	-DCONFIG_16BIT_CHECKSUM \
	-DCONFIG_GALOIS_MUL_LUT \
	-DGETTEXT_PACKAGE='"pgm"'

libpgm_la_LDFLAGS = -release 5.1 -version-info 0:101

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status libtool

# eof

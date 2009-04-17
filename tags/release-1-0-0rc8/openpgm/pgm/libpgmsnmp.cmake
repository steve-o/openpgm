# libpgmsnmp.cmake

SET(SRC
	snmp.c
	pgmMIB.c
)

SET(INC
	include
	${GLIB2_INCLUDE_DIRS}
	${SNMP_INCLUDE_DIRS}
)

SET(CFLAGS
	-DG_LOG_DOMAIN='"Pgm-Snmp"'
)

OPENPGMLIB(pgmsnmp "${SRC}" "${INC}" "${CFLAGS}")

# end of file

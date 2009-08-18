# libpgm.cmake

SET(SRC
	log.c
	backtrace.c
)

SET(INC
	include
	${GLIB2_INCLUDE_DIRS}
)

OPENPGMLIB(pgmex "${SRC}" "${INC}" "")

# end of file

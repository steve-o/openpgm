# libpgm.cmake

SET(SRC
	packet.c
	timer.c
	if.c
	gsi.c
	signal.c
	txwi.c
	rxwi.c
	transport.c
	rate_control.c
	async.c
	checksum.c
	reed_solomon.c
	${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c
)

SET(INC
	include
	${GLIB2_INCLUDE_DIRS}
)

SET(CFLAGS
	-DG_LOG_DOMAIN='"Pgm"'
)

# generated galois tables
ADD_CUSTOM_COMMAND(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c
	COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl > ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl
)

# version stamping
ADD_CUSTOM_COMMAND(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c
	COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py > ${CMAKE_CURRENT_BINARY_DIR}/version.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py
		${SRC}
)
SET(SRC ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/version.c)

OPENPGMLIB(pgm "${SRC}" "${INC}" "${CFLAGS}")

# end of file

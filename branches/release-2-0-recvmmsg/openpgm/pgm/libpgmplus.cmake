# libpgmplus.cmake

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

set(CFLAGS
	-DG_LOG_DOMAIN='"Pgm"'
)

ADD_CUSTOM_COMMAND(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c
	COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl > ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl
)

#IF(CMAKE_SYSTEM_PROCESSOR MATCHES x86_64)
#	MESSAGE(STATUS "Using x86-64 assembler checksum & copy routines.")
#	SET(SRC ${SRC}
#		csum-copy64.S
#		csum-partial64.c
#	)
#	ADD_DEFINITIONS(-DCONFIG_CKSUM_COPY)
#ENDIF(CMAKE_SYSTEM_PROCESSOR MATCHES x86_64)
#
#IF(CMAKE_SYSTEM_PROCESSOR MATCHES "i.*86")
#	MESSAGE(STATUS "Using x86 assembler checksum & copy routines.")
#	SET(SRC ${SRC}
#		checksum32.S
#	)
#	ADD_DEFINITIONS(-DCONFIG_CKSUM_COPY)
#ENDIF(CMAKE_SYSTEM_PROCESSOR MATCHES "i.*86")

# version stamping
ADD_CUSTOM_COMMAND(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py > ${CMAKE_CURRENT_BINARY_DIR}/version.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py
                ${SRC}
)
SET(SRC ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/version.c)

OPENPGMLIB(pgmplus "${SRC}" "${INC}" "${CFLAGS}")

# end of file

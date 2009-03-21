# examples/CMakeLists.txt

MESSAGE(STATUS "Configuring examples")

SET(LIBS
	pgmhttp
	pgmsnmp
	pgmplus
	pgmex
	${GLIB2_LIBRARIES}
	${LIBSOUP_LIBRARIES}
	${SNMP_LIBRARIES}
)

MACRO(PGMEXE
	exe)
	ADD_EXECUTABLE(${exe} ${exe}.c)
	TARGET_LINK_LIBRARIES(${exe} ${LIBS})
ENDMACRO(PGMEXE)

# core examples
PGMEXE(pgmdump)
PGMEXE(pgmsend)
PGMEXE(pgmrecv)
PGMEXE(pgmping)

PGMEXE(pgmtop)
TARGET_LINK_LIBRARIES(pgmtop ncurses panel)

# synchronous examples
PGMEXE(blocksyncrecv)
PGMEXE(snonblocksyncrecv)
PGMEXE(pnonblocksyncrecv)
PGMEXE(enonblocksyncrecv)
PGMEXE(enonblocksyncrecvmsg)
PGMEXE(enonblocksyncrecvmsgv)

# asycnhronous examples
PGMEXE(asyncqrecv)
PGMEXE(blockqrecv)


# end of file

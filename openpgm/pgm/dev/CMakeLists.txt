# dev/CMakeLists.txt

MESSAGE(STATUS "Configuring development code")

SET(LIBS
	pgmplus
	pgmex
	${GLIB2_LIBRARIES}
)

SET(MGMTLIBS
	pgmhttp
	pgmsnmp
	${LIBSOUP_LIBRARIES}
	${SNMP_LIBRARIES}
)

MACRO(PGMEXE
	appname
	src)
	ADD_EXECUTABLE(${appname} ${src})
	TARGET_LINK_LIBRARIES(${appname} ${LIBS})
ENDMACRO(PGMEXE)

MACRO(MGMTEXE
	appname
	src)
	ADD_EXECUTABLE(${appname} ${src})
	TARGET_LINK_LIBRARIES(${appname} ${LIBS} ${MGMTLIBS})
ENDMACRO(MGMTEXE)

MGMTEXE(basic_recv basic_recv.c)
MGMTEXE(basic_http basic_http.c)
MGMTEXE(basic_recv_with_nak basic_recv_with_nak.c)
MGMTEXE(basic_recv_with_rxw basic_recv_with_rxw.c)

PGMEXE(basic_send basic_send.c)
PGMEXE(spm_idle spm_idle.c)
PGMEXE(stream_send stream_send.c)
PGMEXE(basic_container basic_container.c)
PGMEXE(basic_container2 basic_container2.c)
PGMEXE(basic_txw basic_txw.c)
PGMEXE(nak_txw nak_txw.c)
PGMEXE(stream_send_with_nak stream_send_with_nak.c)
PGMEXE(dumpif dumpif.c)
PGMEXE(testif testif.c)
PGMEXE(sw_calc sw_calc.c)
PGMEXE(basic_rxw basic_rxw.c)
PGMEXE(test_cpu_timers test_cpu_timers.c)
PGMEXE(test_time test_time.c)
PGMEXE(block_send block_send.c)
PGMEXE(test_rs test_rs.c)
PGMEXE(test_rs2 test_rs2.c)
PGMEXE(sock_name sock_name.c)
PGMEXE(pgmversion pgmversion.c)

# end of file

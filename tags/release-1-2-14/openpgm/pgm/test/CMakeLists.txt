# test/CMakeLists.txt

MESSAGE(STATUS "Configuring networking tests")

SET(LIBS
	pgmplus
	pgmex
	${GLIB2_LIBRARIES}
)

ADD_EXECUTABLE(monitor monitor.c dump-json.c)
TARGET_LINK_LIBRARIES(monitor ${LIBS})

ADD_EXECUTABLE(app app.c)
TARGET_LINK_LIBRARIES(app ${LIBS})

ADD_EXECUTABLE(sim sim.c dump-json.c)
TARGET_LINK_LIBRARIES(sim ${LIBS})


# end of file

# check/CMakeLists.txt

MESSAGE(STATUS "Configuring check unit tests")

ADD_EXECUTABLE(check_pgm check_pgm.c check_send.c check_if.c)
TARGET_LINK_LIBRARIES(check_pgm pgm pgmex ${GLIB2_LIBRARIES} ${CHECK_LIBRARIES})

ADD_EXECUTABLE(check_pgmplus check_pgm.c check_send.c check_if.c)
TARGET_LINK_LIBRARIES(check_pgmplus pgmplus pgmex ${GLIB2_LIBRARIES} ${CHECK_LIBRARIES})

# end of file

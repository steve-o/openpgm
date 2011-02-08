# CMake build script for OpenPGM on Windows

cmake_minimum_required (VERSION 2.8)

project (OpenPGM)

#-----------------------------------------------------------------------------
# force off-tree build

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
message(FATAL_ERROR "CMake generation is not allowed within the source directory! 
Remove the CMakeCache.txt file and try again from another folder, e.g.: 

   del CMakeCache.txt 
   mkdir cmake-make 
   cd cmake-make
   cmake ..
")
endif(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

#-----------------------------------------------------------------------------
# dependencies

find_package(PythonInterp REQUIRED)
find_package(Perl REQUIRED)
find_program(PATCH_EXECUTABLE patch)

#-----------------------------------------------------------------------------
# default to Release build

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH  ${CMAKE_BINARY_DIR}/lib)

#-----------------------------------------------------------------------------
# platform specifics

add_definitions(
	-DWIN32
	-DCONFIG_HAVE_ISO_VARARGS
	-DCONFIG_HAVE_TSC
	-DCONFIG_HAVE_WSACMSGHDR
	-DCONFIG_HAVE_DSO_VISIBILITY
	-DCONFIG_HAVE_SECURITY_ENHANCED_CRT
	-DCONFIG_BIND_INADDR_ANY
)

#-----------------------------------------------------------------------------
# source files

set(c99-sources
        thread.c
        mem.c
        string.c
        list.c
        slist
        queue.c
        hashtable.c
        messages.c
        error.c
        math.c
        packet_parse.c
        packet_test.c
        sockaddr.c
        time.c
        if.c
        getifaddrs.c
	get_nprocs.c
        getnetbyname.c
        getnodeaddr.c
        getprotobyname.c
        indextoaddr.c
        indextoname.c
        nametoindex.c
        inet_network.c
        md5.c
        rand.c
        gsi.c
        tsi.c
        txw.c
        rxw.c
        skbuff.c
        socket.c
        source.c
        receiver.c
        recv.c
        engine.c
        timer.c
        net.c
        rate_control.c
        checksum.c
        reed_solomon.c
        wsastrerror.c
        histogram.c
)

include_directories(
	include
)
set(headers
	include/pgm/atomic.h
	include/pgm/engine.h
	include/pgm/error.h
	include/pgm/gsi.h
	include/pgm/if.h
	include/pgm/in.h
	include/pgm/list.h
	include/pgm/macros.h
	include/pgm/mem.h
	include/pgm/messages.h
	include/pgm/msgv.h
	include/pgm/packet.h
	include/pgm/pgm.h
	include/pgm/skbuff.h
	include/pgm/socket.h
	include/pgm/time.h
	include/pgm/tsi.h
	include/pgm/types.h
	include/pgm/version.h
	include/pgm/winint.h
	include/pgm/wininttypes.h
	include/pgm/zinttypes.h
)

add_definitions(
	-DCONFIG_16BIT_CHECKSUM
	-DCONFIG_TICKET_SPINLOCK
	-DCONFIG_TICKET_RWSPINLOCK
	-DCONFIG_GALOIS_MUL_LUT
	-DGETTEXT_PACKAGE='"pgm"'
)

#-----------------------------------------------------------------------------
# source generators

foreach (source ${c99-sources})
	string(REGEX REPLACE "\\.c$" "" source "${source}")
	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source}.c.c89.patch)
		add_custom_command(
			OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c
			COMMAND ${CMAKE_COMMAND}
			ARGS	-E
				copy
				${CMAKE_CURRENT_SOURCE_DIR}/${source}.c
				${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c
			COMMAND ${PATCH_EXECUTABLE}
			ARGS	--binary
				-i
				${CMAKE_CURRENT_SOURCE_DIR}/${source}.c.c89.patch
				${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c
			DEPENDS ${source}.c
				${CMAKE_CURRENT_SOURCE_DIR}/${source}.c.c89.patch
		)
	else(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source}.c.c89.patch)
		add_custom_command(
			OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c
			COMMAND ${CMAKE_COMMAND}
			ARGS	-E
				copy
				${CMAKE_CURRENT_SOURCE_DIR}/${source}.c
				${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c
			DEPENDS ${source}.c
		)
	endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source}.c.c89.patch)
	list(APPEND generated-results ${CMAKE_CURRENT_BINARY_DIR}/${source}.c89.c)
endforeach()

# generated galois tables
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c89.c
	COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl > ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c89.c
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/galois_generator.pl
)

# version stamping
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c89.c
	COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py > ${CMAKE_CURRENT_BINARY_DIR}/version.c89.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version_generator.py
)

set(sources
        ${CMAKE_CURRENT_BINARY_DIR}/galois_tables.c89.c
        ${CMAKE_CURRENT_BINARY_DIR}/version.c89.c
        ${generated-results}
)

#-----------------------------------------------------------------------------
# output

add_library(libpgm STATIC ${sources})

set(docs
	COPYING
	LICENSE
	README
)
file(GLOB mibs "${CMAKE_CURRENT_SOURCE_DIR}/mibs/*.txt")
set(examples
	examples/async.c
	examples/async.h
	examples/daytime.c
	examples/getopt.c
	examples/getopt.h
	examples/purinrecv.c
	examples/purinsend.c
	examples/shortcakerecv.c
)

install (TARGETS libpgm DESTINATION lib)
install (FILES ${headers} DESTINATION include/pgm)
install (FILES ${docs} DESTINATION doc)
install (FILES ${mibs} DESTINATION mibs)
install (FILES ${examples} DESTINATION examples)

include (InstallRequiredSystemLibraries)
set (CPACK_PACKAGE_VENDOR "Miru Limited")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_PACKAGE_VERSION_MAJOR "5")
set (CPACK_PACKAGE_VERSION_MINOR "1")
set (CPACK_PACKAGE_VERSION_PATCH "104")
include (CPack)

# end of file

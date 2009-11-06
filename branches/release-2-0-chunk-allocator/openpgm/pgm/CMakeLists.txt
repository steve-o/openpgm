# top level CMake build script for OpenPGM

cmake_minimum_required(VERSION 2.4)

# CMake 2.4 linking forward compatibility
if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

project(OpenPGM)

#-----------------------------------------------------------------------------
# force off-tree build

IF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
MESSAGE(FATAL_ERROR "CMake generation is not allowed within the source directory! 
Remove the CMakeCache.txt file and try again from another folder, e.g.: 

   rm CMakeCache.txt 
   mkdir cmake-make 
   cd cmake-make
   cmake ..
")
ENDIF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

#-----------------------------------------------------------------------------
# default to Debug build

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE Debug CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
ENDIF(NOT CMAKE_BUILD_TYPE)


SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH  ${CMAKE_BINARY_DIR}/lib)

#-----------------------------------------------------------------------------
# Dependencies

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_ROOT}/Modules ${CMAKE_SOURCE_DIR}/CMake)
INCLUDE(FindPkgConfig)
pkg_check_modules (GLIB2 REQUIRED	glib-2.0>=2.10 gthread-2.0)
pkg_check_modules (LIBSOUP REQUIRED	libsoup-2.4)
pkg_check_modules (CHECK 		check)

#-----------------------------------------------------------------------------
# Platform specifics

SET(PLATFORM_CFLAGS " 	-pipe
			-Wall
				-Wextra
				-Wfloat-equal
				-Wshadow
				-Wunsafe-loop-optimizations
				-Wpointer-arith
				-Wbad-function-cast
				-Wcast-qual
				-Wcast-align
				-Wwrite-strings
				-Waggregate-return
				-Wstrict-prototypes
				-Wold-style-definition
				-Wmissing-prototypes
				-Wmissing-declarations
				-Wmissing-noreturn
				-Wmissing-format-attribute
				-Wredundant-decls
				-Wnested-externs
				-Winline
			-pedantic
			-std=gnu99
			--param max-inline-insns-single=600
			-D_REENTRANT
			-D_GNU_SOURCE
			-D__need_IOV_MAX
			-DCONFIG_HAVE_EPOLL
			-DCONFIG_HAVE_RTC
			-DCONFIG_HAVE_TSC
			-DCONFIG_HAVE_IFR_NETMASK
			-DCONFIG_HAVE_GETIFADDRS
			-DCONFIG_HAVE_GETHOSTBYNAME2
			-DCONFIG_HAVE_GETPROTOBYNAME_R
			-DCONFIG_BIND_INADDR_ANY
			-DCONFIG_GALOIS_MUL_LUT
			-DCONFIG_WITH_SNMP
			-DCONFIG_WITH_HTTP
")
STRING(REGEX REPLACE "[\t\n ]+" " " PLATFORM_CFLAGS "${PLATFORM_CFLAGS}")
SET(PLATFORM_LINKFLAGS	"-pipe")

SET(SNMP_CFLAGS, "")
SET(SNMP_LIBRARIES
	netsnmpagent
	netsnmpmibs
	netsnmphelpers
	netsnmp
)

# fix CMake bug detecting arch
IF(CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
	EXEC_PROGRAM(uname ARGS -m OUTPUT_VARIABLE CMAKE_SYSTEM_PROCESSOR
		RETURN_VALUE val)
	IF("${val}" GREATER 0)
		SET(CMAKE_SYSTEM_PROCESSOR "unknown")
	ENDIF("${val}" GREATER 0)
ENDIF(CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")


# collate platform parameters
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PLATFORM_CFLAGS}")

#-----------------------------------------------------------------------------

INCLUDE(CMake/macros.cmake)

# libraries
INCLUDE(libpgm.cmake)
INCLUDE(libpgmplus.cmake)
INCLUDE(libpgmhttp.cmake)
INCLUDE(libpgmsnmp.cmake)

# unit tests
IF(CHECK_FOUND)
	ADD_SUBDIRECTORY(check)
ENDIF(CHECK_FOUND)

# network tests
ADD_SUBDIRECTORY(test)

# development code
ADD_SUBDIRECTORY(dev)

# examples
INCLUDE(libpgmex.cmake)
ADD_SUBDIRECTORY(examples)

# end of file

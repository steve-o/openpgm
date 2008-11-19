# libpgmhttp.cmake

SET(SRC http.c)

SET(HTDOCS
	404.html
	base.css
	robots.txt
	xhtml10_strict.doctype
)

SET(INC
	include
	${CMAKE_CURRENT_BINARY_DIR}
	${GLIB2_INCLUDE_DIRS}
	${LIBSOUP_INCLUDE_DIRS}
)

MACRO(PACKHTDOC
	htdoc)
	ADD_CUSTOM_COMMAND(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/htdocs/${htdoc}.h"
		COMMAND mkdir -p "${CMAKE_CURRENT_BINARY_DIR}/htdocs"
		COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/htdocs/convert_to_macro.pl "${CMAKE_CURRENT_SOURCE_DIR}/htdocs/${htdoc}" > "${CMAKE_CURRENT_BINARY_DIR}/htdocs/${htdoc}.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/htdocs/${htdoc}"
	)
ENDMACRO(PACKHTDOC)

FOREACH(htdoc ${HTDOCS})
	PACKHTDOC(${htdoc})
ENDFOREACH(htdoc HTDOCS)

SET(CFLAGS
	-DG_LOG_DOMAIN='"Pgm-Http"'
)

OPENPGMLIB(pgmhttp "${SRC}" "${INC}" "${CFLAGS}")

# end of file

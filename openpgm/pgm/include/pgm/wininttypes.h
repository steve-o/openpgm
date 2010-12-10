/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * inttypes.h for Win32 & Win64
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_WININTTYPES_H__
#define __PGM_WININTTYPES_H__

#if !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS)

/* 7.8.1 Macros for format specifiers
 * 
 * MS runtime does not yet understand C9x standard "ll"
 * length specifier. It appears to treat "ll" as "l".
 * The non-standard I64 length specifier causes warning in GCC,
 * but understood by MS runtime functions.
 */

/* fprintf macros for signed types */
#define PRId8		"d"
#define PRId16		"hd"
#define PRId32		"I32d"
#define PRId64		"I64d"

#define PRIdLEAST8	"d"
#define PRIdLEAST16	"hd"
#define PRIdLEAST32	"I32d"
#define PRIdLEAST64	"I64d"

#define PRIdFAST8	"d"
#define PRIdFAST16	"hd"
#define PRIdFAST32	"I32d"
#define PRIdFAST64	"I64d"

#define PRIdMAX		"I64d"

#define PRIi8		"i"
#define PRIi16		"hi"
#define PRIi32		"i"
#define PRIi64		"I64i"

#define PRIiLEAST8	"i"
#define PRIiLEAST16	"hi"
#define PRIiLEAST32	"I32i"
#define PRIiLEAST64	"I64i"

#define PRIiFAST8	"i"
#define PRIiFAST16	"hi"
#define PRIiFAST32	"I32i"
#define PRIiFAST64	"I64i"

#define PRIiMAX		"I64i"

#define PRIo8		"o"
#define PRIo16		"ho"
#define PRIo32		"I32o"
#define PRIo64		"I64o"

#define PRIoLEAST8	"o"
#define PRIoLEAST16	"ho"
#define PRIoLEAST32	"I32o"
#define PRIoLEAST64	"I64o"

#define PRIoFAST8	"o"
#define PRIoFAST16	"ho"
#define PRIoFAST32	"o"
#define PRIoFAST64	"I64o"

#define PRIoMAX		"I64o"

/* fprintf macros for unsigned types */
#define PRIu8		"u"
#define PRIu16		"hu"
#define PRIu32		"I32u"
#define PRIu64		"I64u"

#define PRIuLEAST8	"u"
#define PRIuLEAST16	"hu"
#define PRIuLEAST32	"I32u"
#define PRIuLEAST64	"I64u"

#define PRIuFAST8	"u"
#define PRIuFAST16	"hu"
#define PRIuFAST32	"I32u"
#define PRIuFAST64	"I64u"

#define PRIuMAX		"I64u"

#define PRIx8		"x"
#define PRIx16		"hx"
#define PRIx32		"I32x"
#define PRIx64		"I64x"

#define PRIxLEAST8	"x"
#define PRIxLEAST16	"hx"
#define PRIxLEAST32	"I32x"
#define PRIxLEAST64	"I64x"

#define PRIxFAST8	"x"
#define PRIxFAST16	"hx"
#define PRIxFAST32	"I32x"
#define PRIxFAST64	"I64x"

#define PRIxMAX		"I64x"

#define PRIX8		"X"
#define PRIX16		"hX"
#define PRIX32		"I32X"
#define PRIX64		"I64X"

#define PRIXLEAST8	"X"
#define PRIXLEAST16	"hX"
#define PRIXLEAST32	"I32X"
#define PRIXLEAST64	"I64X"

#define PRIXFAST8	"X"
#define PRIXFAST16	"hX"
#define PRIXFAST32	"I32X"
#define PRIXFAST64	"I64X"

#define PRIXMAX		"I64X"

/*  fscanf macros for signed int types */

#define SCNd8           "hhd"
#define SCNd16		"hd"
#define SCNd32		"ld"
#define SCNd64		"I64d"

#define SCNdLEAST8      "hhd"
#define SCNdLEAST16	"hd"
#define SCNdLEAST32	"ld"
#define SCNdLEAST64	"I64d"

#define SCNdFAST8       "hhd"
#define SCNdFAST16	"hd"
#define SCNdFAST32	"ld"
#define SCNdFAST64	"I64d"

#define SCNdMAX		"I64d"

#define SCNi8           "hhi"
#define SCNi16		"hi"
#define SCNi32		"li"
#define SCNi64		"I64i"

#define SCNiLEAST8      "hhi"
#define SCNiLEAST16	"hi"
#define SCNiLEAST32	"li"
#define SCNiLEAST64	"I64i"

#define SCNiFAST8       "hhi"
#define SCNiFAST16	"hi"
#define SCNiFAST32	"li"
#define SCNiFAST64	"I64i"

#define SCNiMAX		"I64i"

#define SCNo8           "hho"
#define SCNo16		"ho"
#define SCNo32		"lo"
#define SCNo64		"I64o"

#define SCNoLEAST8      "hho"
#define SCNoLEAST16	"ho"
#define SCNoLEAST32	"lo"
#define SCNoLEAST64	"I64o"

#define SCNoFAST8       "hho"
#define SCNoFAST16	"ho"
#define SCNoFAST32	"lo"
#define SCNoFAST64	"I64o"

#define SCNoMAX		"I64o"

#define SCNx8           "hhx"
#define SCNx16		"hx"
#define SCNx32		"lx"
#define SCNx64		"I64x"

#define SCNxLEAST8      "hhx"
#define SCNxLEAST16	"hx"
#define SCNxLEAST32	"lx"
#define SCNxLEAST64	"I64x"

#define SCNxFAST8       "hhx"
#define SCNxFAST16	"hx"
#define SCNxFAST32	"lx"
#define SCNxFAST64	"I64x"

#define SCNxMAX		"I64x"

/* fscanf macros for unsigned int types */

#define SCNu8           "hhu"
#define SCNu16		"hu"
#define SCNu32		"lu"
#define SCNu64		"I64u"

#define SCNuLEAST8      "hhu"
#define SCNuLEAST16	"hu"
#define SCNuLEAST32	"lu"
#define SCNuLEAST64	"I64u"

#define SCNuFAST8       "hhu"
#define SCNuFAST16	"hu"
#define SCNuFAST32	"lu"
#define SCNuFAST64	"I64u"

#define SCNuMAX		"I64u"

#ifdef _WIN64
#	define PRIdPTR		"I64d"
#	define PRIiPTR		"I64i"
#	define PRIoPTR		"I64o"
#	define PRIuPTR		"I64u"
#	define PRIxPTR 		"I64x"
#	define PRIXPTR 		"I64X"
#	define SCNdPTR 		"I64d"
#	define SCNiPTR 		"I64i"
#	define SCNoPTR 		"I64o"
#	define SCNxPTR 		"I64x"
#	define SCNuPTR 		"I64u"
#else
#	define PRIdPTR 		"ld"
#	define PRIiPTR 		"li"
#	define PRIoPTR 		"lo"
#	define PRIuPTR 		"lu"
#	define PRIxPTR 		"lx"
#	define PRIXPTR 		"lX"
#	define SCNdPTR 		"ld"
#	define SCNiPTR 		"li"
#	define SCNoPTR 		"lo"
#	define SCNxPTR 		"lx"
#	define SCNuPTR 		"lu"
#endif

#endif	/* !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS) */

#endif /* __PGM_WININTTYPES_H__ */

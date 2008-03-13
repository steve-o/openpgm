/* IP/TCP/UDP checksumming routines
 *
 * Authors:     Jorge Cwik, <jorge@laser.satlink.net>
 *              Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *              Tom May, <ftom@netcom.com>
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *              Lots of code moved from tcp.c and ip.c; see those files
 *              for more names.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */
	#include <asm/errno.h>

#define __ALIGN         .align 4,0x90
#define ALIGN __ALIGN
#define ENTRY(name) \
  .globl name; \
  ALIGN; \
  name:
#define END(name) \
  .size name, .-name
#define ENDPROC(name) \
  .type name, @function; \
  END(name)

#define CFI_STARTPROC .cfi_startproc
#define CFI_ENDPROC .cfi_endproc
#define CFI_DEF_CFA .cfi_def_cfa
#define CFI_DEF_CFA_REGISTER .cfi_def_cfa_register
#define CFI_DEF_CFA_OFFSET .cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET .cfi_adjust_cfa_offset
#define CFI_OFFSET .cfi_offset
#define CFI_REL_OFFSET .cfi_rel_offset
#define CFI_REGISTER .cfi_register
#define CFI_RESTORE .cfi_restore
#define CFI_REMEMBER_STATE .cfi_remember_state
#define CFI_RESTORE_STATE .cfi_restore_state
#define CFI_UNDEFINED .cfi_undefined

.text

/* Pentium-Pro and above happy version 
 */

ENTRY(csum_partial)
	CFI_STARTPROC
	pushl %esi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET esi, 0
	pushl %ebx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebx, 0
	movl 20(%esp),%eax	# Function arg: unsigned int sum
	movl 16(%esp),%ecx	# Function arg: int len
	movl 12(%esp),%esi	# Function arg:	const unsigned char *buf

	testl $3, %esi         
	jnz 25f                 
10:
	movl %ecx, %edx
	movl %ecx, %ebx
	andl $0x7c, %ebx
	shrl $7, %ecx
	addl %ebx,%esi
	shrl $2, %ebx  
	negl %ebx
	lea 45f(%ebx,%ebx,2), %ebx
	testl %esi, %esi
	jmp *%ebx

	# Handle 2-byte-aligned regions
20:	addw (%esi), %ax
	lea 2(%esi), %esi
	adcl $0, %eax
	jmp 10b
25:
	testl $1, %esi         
	jz 30f                 
	# buf is odd
	dec %ecx
	jl 90f
	movzbl (%esi), %ebx
	addl %ebx, %eax
	adcl $0, %eax
	roll $8, %eax
	inc %esi
	testl $2, %esi
	jz 10b

30:	subl $2, %ecx          
	ja 20b                 
	je 32f
	addl $2, %ecx
	jz 80f
	movzbl (%esi),%ebx	# csumming 1 byte, 2-aligned
	addl %ebx, %eax
	adcl $0, %eax
	jmp 80f
32:
	addw (%esi), %ax	# csumming 2 bytes, 2-aligned
	adcl $0, %eax
	jmp 80f

40: 
	addl -128(%esi), %eax
	adcl -124(%esi), %eax
	adcl -120(%esi), %eax
	adcl -116(%esi), %eax   
	adcl -112(%esi), %eax   
	adcl -108(%esi), %eax
	adcl -104(%esi), %eax
	adcl -100(%esi), %eax
	adcl -96(%esi), %eax
	adcl -92(%esi), %eax
	adcl -88(%esi), %eax
	adcl -84(%esi), %eax
	adcl -80(%esi), %eax
	adcl -76(%esi), %eax
	adcl -72(%esi), %eax
	adcl -68(%esi), %eax
	adcl -64(%esi), %eax     
	adcl -60(%esi), %eax     
	adcl -56(%esi), %eax     
	adcl -52(%esi), %eax   
	adcl -48(%esi), %eax   
	adcl -44(%esi), %eax
	adcl -40(%esi), %eax
	adcl -36(%esi), %eax
	adcl -32(%esi), %eax
	adcl -28(%esi), %eax
	adcl -24(%esi), %eax
	adcl -20(%esi), %eax
	adcl -16(%esi), %eax
	adcl -12(%esi), %eax
	adcl -8(%esi), %eax
	adcl -4(%esi), %eax
45:
	lea 128(%esi), %esi
	adcl $0, %eax
	dec %ecx
	jge 40b
	movl %edx, %ecx
50:	andl $3, %ecx
	jz 80f

	# Handle the last 1-3 bytes without jumping
	notl %ecx		# 1->2, 2->1, 3->0, higher bits are masked
	movl $0xffffff,%ebx	# by the shll and shrl instructions
	shll $3,%ecx
	shrl %cl,%ebx
	andl -128(%esi),%ebx	# esi is 4-aligned so should be ok
	addl %ebx,%eax
	adcl $0,%eax
80: 
	testl $1, 12(%esp)
	jz 90f
	roll $8, %eax
90: 
	popl %ebx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ebx
	popl %esi
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE esi
	ret
	CFI_ENDPROC
ENDPROC(csum_partial)
				

#define SRC(y...)			\
	9999: y;			\
	.section __ex_table, "a";	\
	.long 9999b, 6001f	;	\
	.previous

#define DST(y...)			\
	9999: y;			\
	.section __ex_table, "a";	\
	.long 9999b, 6002f	;	\
	.previous

#define ROUND1(x) \
	SRC(movl x(%esi), %ebx	)	;	\
	addl %ebx, %eax			;	\
	DST(movl %ebx, x(%edi)	)	; 

#define ROUND(x) \
	SRC(movl x(%esi), %ebx	)	;	\
	adcl %ebx, %eax			;	\
	DST(movl %ebx, x(%edi)	)	;

#define ARGBASE 12
		
ENTRY(csum_partial_copy_generic)
	CFI_STARTPROC
	pushl %ebx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebx, 0
	pushl %edi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edi, 0
	pushl %esi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET esi, 0
	movl ARGBASE+4(%esp),%esi	#src
	movl ARGBASE+8(%esp),%edi	#dst	
	movl ARGBASE+12(%esp),%ecx	#len
	movl ARGBASE+16(%esp),%eax	#sum
#	movl %ecx, %edx  
	movl %ecx, %ebx  
	movl %esi, %edx
	shrl $6, %ecx     
	andl $0x3c, %ebx  
	negl %ebx
	subl %ebx, %esi  
	subl %ebx, %edi  
	lea  -1(%esi),%edx
	andl $-32,%edx
	lea 3f(%ebx,%ebx), %ebx
	testl %esi, %esi 
	jmp *%ebx
1:	addl $64,%esi
	addl $64,%edi 
	SRC(movb -32(%edx),%bl)	; SRC(movb (%edx),%bl)
	ROUND1(-64) ROUND(-60) ROUND(-56) ROUND(-52)	
	ROUND (-48) ROUND(-44) ROUND(-40) ROUND(-36)	
	ROUND (-32) ROUND(-28) ROUND(-24) ROUND(-20)	
	ROUND (-16) ROUND(-12) ROUND(-8)  ROUND(-4)	
3:	adcl $0,%eax
	addl $64, %edx
	dec %ecx
	jge 1b
4:	movl ARGBASE+12(%esp),%edx	#len
	andl $3, %edx
	jz 7f
	cmpl $2, %edx
	jb 5f
SRC(	movw (%esi), %dx         )
	leal 2(%esi), %esi
DST(	movw %dx, (%edi)         )
	leal 2(%edi), %edi
	je 6f
	shll $16,%edx
5:
SRC(	movb (%esi), %dl         )
DST(	movb %dl, (%edi)         )
6:	addl %edx, %eax
	adcl $0, %eax
7:
.section .fixup, "ax"
6001:	movl	ARGBASE+20(%esp), %ebx	# src_err_ptr	
	movl $-EFAULT, (%ebx)
	# zero the complete destination (computing the rest is too much work)
	movl ARGBASE+8(%esp),%edi	# dst
	movl ARGBASE+12(%esp),%ecx	# len
	xorl %eax,%eax
	rep; stosb
	jmp 7b
6002:	movl ARGBASE+24(%esp), %ebx	# dst_err_ptr
	movl $-EFAULT, (%ebx)
	jmp  7b			
.previous				

	popl %esi
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE esi
	popl %edi
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE edi
	popl %ebx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ebx
	ret
	CFI_ENDPROC
ENDPROC(csum_partial_copy_generic)
				
#undef ROUND
#undef ROUND1		


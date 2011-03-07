; Copyright (c) Miru Limited.

.486
.model flat, c

INCLUDE intrin.inc

.code

; BYTE __InterlockedExchangeAdd8(		/rax/
;	__inout	BYTE volatile *Addend,		/rcx/
;	__in	BYTE Value			/rdx/
; );
__InterlockedExchangeAdd8 PROC
	mov eax, edx
	lock xadd byte ptr [rcx], al
	add eax, edx
	ret
__InterlockedExchangeAdd8 ENDPROC

; BYTE __InterlockedIncrement8(			/rax/
;	__inout BYTE volatile *Addend		/rcx/
; );
__InterlockedIncrement8 PROC
	mov eax, 1
	lock xadd byte ptr [rcx], al
	add eax, edx
	ret
__InterlockedIncrement8 ENDPROC

; SHORT __InterlockedExchangeAdd16(		/rax/
;	__inout	SHORT volatile *Addend,		/rcx/
;	__in	SHORT Value			/rdx/
; );
__InterlockedExchangeAdd16 PROC
	mov ax, dx
	lock xadd word ptr [rcx], ax
	add ax, dx
	ret
__InterlockedExchangeAdd16 ENDPROC

; SHORT __InterlockedIncrement16(		/rax/
;	__inout SHORT volatile *Addend		/rcx/
; );
__InterlockedIncrement16 PROC
	mov ax, 1
	lock xadd word ptr [rcx], ax
	add ax, dx
	ret
__InterlockedIncrement16 ENDPROC

end

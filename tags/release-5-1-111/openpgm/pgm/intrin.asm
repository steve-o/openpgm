; Copyright (c) Miru Limited.

INCLUDE intrin.inc

.code

; BYTE __InterlockedExchangeAdd8(		/rax/
;	__inout	BYTE volatile *Addend,		/rcx/
;	__in	BYTE Value			/rdx/
; );
__InterlockedExchangeAdd8 PROC
	mov rax, rdx
	lock xadd byte ptr [rcx], al
	add rax, rdx
	ret
__InterlockedExchangeAdd8 ENDP

; BYTE __InterlockedIncrement8(			/rax/
;	__inout BYTE volatile *Addend		/rcx/
; );
__InterlockedIncrement8 PROC
	mov rax, 1
	lock xadd byte ptr [rcx], al
	add rax, 1
	ret
__InterlockedIncrement8 ENDP

; SHORT __InterlockedExchangeAdd16(		/rax/
;	__inout	SHORT volatile *Addend,		/rcx/
;	__in	SHORT Value			/rdx/
; );
__InterlockedExchangeAdd16 PROC
	mov rax, rdx
	lock xadd word ptr [rcx], ax
	add rax, rdx
	ret
__InterlockedExchangeAdd16 ENDP

; SHORT __InterlockedIncrement16(		/rax/
;	__inout SHORT volatile *Addend		/rcx/
; );
__InterlockedIncrement16 PROC
	mov rax, 1
	lock xadd word ptr [rcx], ax
	add rax, 1
	ret
__InterlockedIncrement16 ENDP

end

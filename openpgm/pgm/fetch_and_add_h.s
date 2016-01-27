# Implementation of AIX fetch_and_add_h().
#
# fetch_and_add_h() is declared in sys/atomic_op.h on AIX 5.2 but
# isn't in the standard runtime libraries. fetch_and_add() is present.
# So provide an implementation based on the GCC __sync_fetch_and_add().
#
# xlc V6 (yes, old, I know) doesn't do inline assembler, hence the need
# to put this in a separate source file.
        
        .toc
        .file "fetch_and_add_h.s"

        .globl .fetch_and_add_h

        .csect fetchaddh{PR}
        # short fetch_and_add_h(*short p, short v)
        # {
        #    short res = *p;
        #    *p += v;
        #    return res;
        # }
.fetch_and_add_h:
        lwsync

        li      0, 0
        ori     0, 0, 0xffff          # R0 = 0xffff

        # lwarx/stwcx deal in 32bit words. But the pointer to the
        # value of interest will be on a 16 or 32bit boundary.
        # So we will load the 32bit words containing it, and note
        # whether we need to deal with the high or low 16 bit short
        # therein. First we set R9 to a shift (16 or 0) and R0 to a mask.
        # If on a 16bit boundary, the quantity of interest will be in the
        # low half of the 32bit word, and the high half on a 32bit boundary.
        #
        # So start by setting R9 to 16 if short is high part of word,
        # 0 if low part. Also set R0 to the matching mask.
        rlwinm  9, 3, 3, 27, 27       # Bit 1 of pointer, shift left 3.
        xori    9, 9, 16
        slw     0, 0, 9

        # Adjust R3 to the address of the 32bit word
        rlwinm  3, 3, 0, 0, 29

        # Move the value to be added to the matching half word.
        slw     4, 4, 9

        # Grab the word of interest into R11, add into R10,
        # and replace the non-result half with the original content.
again:
        lwarx   11, 0, 3
        add     10, 11, 4
        and     10, 10, 0
        andc    8, 11, 0
        or      10, 10, 8

        # Try the store, loop if fails.
        stwcx.  10, 0, 3
        bne     again

        # Written new value, make sure all further reads get it.
        isync

        # Get the half of the original value to return into R9.
        slw     9, 11, 9
        rlwinm  3, 9, 0, 0xffff
        blr

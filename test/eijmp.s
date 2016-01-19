; regression - test the eijmp instruction
; this should generate a breakpoint (no EIND) or idle (eijmp)

.text
    ldi r31, hi8(pm(1f))
    ldi r30, lo8(pm(1f))
    inc r0
    out 0x3C, r0
    eijmp
    break
.p2align 17
    .rept 5
    nop
    .endr
1:  sleep

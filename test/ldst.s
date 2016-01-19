; regression - test push/pop, st-incr/decr

.text
    cli
    ldi r16, 0xDE
    ldi r17, 0xAD
    ldi r18, 0xBE
    ldi r19, 0xEF
    push r16
    lds r31, 0x5E ; SP
    lds r30, 0x5D
    ld r0, Z
    pop r1
    clr r31
    clr r30
    st X+, r16
    st X+, r17
    st Y+, r18
    st Y+, r19
    st Z+, r16
    st Z+, r17
    st -Z, r18
    st -Z, r19
    st -Y, r16
    st -Y, r17
    st -X, r18
    st -X, r19
    sleep

; various tests covering common instructions

.text

    ldi r28, 7
    clr r29
    ldi r16, 1
    std Y+24, r16
1:
    add r16, r16
    clr r0
    add r0, r16
    mov r1, r0
    add r0, r16
    add r0, r0
    add r1, r0
    movw r2, r0
    sub r0, r1
    subi r16, 2
    bst r16, 1
    bld r15, 4
    brcs 1f
    rjmp 1b
1:
    ldd r31, Y+9
    ldi r16, 0xCE
    mov r7, r16
    ldi r16, 0xAF
    st Y+, r16
    push r16
    pop r3
    ldi r31, hi8(datum)
    ldi r30, lo8(datum)
    lpm r16, Z+
    ldi r16, 8
    ldi r17, 8
    mul r16, r17
    subi r20, 1
    sbci r20, 0
    rcall fun
    adiw r24, 0x3F
    adiw r24, 0x3F
    adiw r24, 0x3F
    adiw r24, 0x3F
    adiw r24, 0x3F
    sleep
datum:
    .byte 0x66
    .byte 0x00
fun:
    ldi r16, 0xAA
    ret

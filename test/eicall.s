; regression - tests the 3-byte Program Counter on the stack; true spaghetti

.text
    out 0x3C, r0
    inc r0
    eicall
    call 1f
    cpse r0, r1
    sleep
    rcall 2f
    break
    ret

2:  in r30, 0x3D
    in r31, 0x3E
    subi r30, -3
    ld r16, Z
    inc r16
    st Z, r16
    ret

.p2align 17
1:  dec r0
    ret

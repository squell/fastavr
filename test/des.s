    break
    ldi r16, 0xef
    mov r0, r16
    ldi r16, 0xcd
    mov r1, r16
    ldi r16, 0xab
    mov r2, r16
    ldi r16, 0x89
    mov r3, r16
    ldi r16, 0x67
    mov r4, r16
    ldi r16, 0x45
    mov r5, r16
    ldi r16, 0x23
    mov r6, r16
    ldi r16, 0x01
    mov r7, r16

    ldi r16, 0xf1
    mov r8, r16
    ldi r16, 0xdf
    mov r9, r16
    ldi r16, 0xbc
    mov r10, r16
    ldi r16, 0x9b
    mov r11, r16
    ldi r16, 0x79
    mov r12, r16
    ldi r16, 0x57
    mov r13, r16
    ldi r16, 0x34
    mov r14, r16
    ldi r16, 0x13
    mov r15, r16

    des 0
    des 1
    des 2
    des 3
    des 4
    des 5
    des 6
    des 7
    des 8
    des 9
    des 10
    des 11
    des 12
    des 13
    des 14
    des 15

    seh

    des 0
    des 1
    des 2
    des 3
    des 4
    des 5
    des 6
    des 7
    des 8
    des 9
    des 10
    des 11
    des 12
    des 13
    des 14
    des 15

    cli
    sleep

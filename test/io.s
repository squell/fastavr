; test accsessing SREG in various ways, as well as  sbi/cbi

.text
    sez
    seh
    in r3, 0x3F
    lds r4, 0x5F
    
    ldi r16, 0x20
    sts 0x5F, r16
    breq 1f
    inc r0
1:  inc r1

    ldi r16, 0x2
    out 0x3F, r16
    breq 1f
    inc r0
1:  inc r1

    in r0, 0x10
    sbi 0x10, 5
    in r0, 0x10
    cbi 0x10, 5
    in r0, 0x10

    sbic 0x10, 5
    rjmp loop
    out 0x10, r16
    sbis 0x10, 1
    rjmp loop

    sleep
loop:
    rjmp loop


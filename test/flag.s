; regression - test the flag conversion

.text
    inc r16
    sub r0, r16
    inc r0
    lds r0, 0x3F
    sleep

; regresion - cpse/skipins
.text
    cli
    inc r1
    inc r1
    sts 0x10, r1
    cpse r0, r2
    inc r0
    cpse r0, r2
    lds r0, 0x10
    cpse r0, r2
    jmp 1f
    sleep
1:  nop
    rjmp 1b

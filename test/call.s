; regression - byte ordering on the stack

.text
    ldi r30, lo8(pm(goto))
    ldi r31, hi8(pm(goto))
    call goto
    dec r0
    sleep
goto:
    inc r0
    pop r1
    pop r2
    push r2
    push r1
    ret

; regression test -- accessing and writing r30 in the LPM instr.

.text
    ldi r31, hi8(goto)
    ldi r30, lo8(goto)
    lpm r30, Z
    sleep
goto:
    .word 0x1234

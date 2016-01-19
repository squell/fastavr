; regression - test the lpm instructions

.text
    ldi r30, lo8(end)
    inc r16
    out 0x3B, r16
    lpm r1, Z+
    lpm r2, Z
    lpm
    dec r30
    elpm r1, Z+
    elpm r2, Z
    elpm
    dec r30
    out 0x3B, r31
    elpm r1, Z+
    elpm r2, Z
    elpm

    sleep
end:
    .word 0x1234

; LD X+ etc should not use the upper byte on devices with < 256 sram

.text
    ldi r30, 0xFF
    clr r31
    ldi r16, 0xAB
    st Z, r16
    ldi r31, 0xFE
    ld r16, Z+
    cli
    sleep

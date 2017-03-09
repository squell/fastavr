start:
    ldi r16, 1
    sts 0x81, r16
    ldi r16, 1
    sts 0x84, r20
    out 0x25, r16
    sts 0x84, r20
    out 0x26, r20
    ldi r16, 100
blaa:
    dec r16
    brne blaa
    lds r0, 0x84
    in r1, 0x26
    sleep


start:
    ldi r16, 1
    out 0x25, r16
    ;out 0x26, r20
    sts 0x46, r20
    in r20, 0x26
    in r20, 0x26
    in r20, 0x26
    in r20, 0x26
    in r20, 0x26
    lds r21, 0x46
    lds r21, 0x46
    sleep

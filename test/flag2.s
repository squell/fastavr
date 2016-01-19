; regression - test the flag set/clear instructions

.text
    sez
    sec
    clc
    breq 1f
    inc r0
1:  inc r1
    sleep

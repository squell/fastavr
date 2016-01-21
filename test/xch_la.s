.text
break
ldi r16, 0xF0
mov r0, r16
ldi r16, 0x55
xch Z, r16
lat Z, r16
las Z, r16
lac Z, r16
sleep

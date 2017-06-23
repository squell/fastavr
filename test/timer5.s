	; checks overflow happens at the right moment
	ldi r16, 0x01
	out 0x25, r16
	ldi r16, 0xf0
	break
	out 0x26, r16
loop:
	in r0, 0x26
	sbis 0x15, 0
	rjmp loop
	sleep

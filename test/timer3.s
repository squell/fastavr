start:
	ldi r16, 4
	sts 0x81, r16
	ldi r16, 1
	out 0x25, r16
	sts 0x84, r20
	out 0x26, r20
	ldi r16, 0xa9/2
bla:
	dec r16
	brne bla
	lds r0, 0x84
	in r1, 0x26
	sleep

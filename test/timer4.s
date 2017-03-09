start:
	ldi r17, 0x81
	out 0x23, r17 ; gtccr
	ldi r16, 1
	sts 0x81, r16 ; tccr1b
	ldi r16, 1
	out 0x25, r16 ; tccr0b
	sts 0x84, r20 ; tcnt1l
	nop
	nop
	nop
	nop
	out 0x26, r20 ; tcnt0
	out 0x23, r16
	ldi r16, 0xa9/2
bla:
	dec r16
	brne bla
	nop
	nop
	nop
	nop
	lds r0, 0x84
	in r1, 0x26
	sleep


# exhaustive test of all mul possibilities

.text

1:
mul r16, r17
muls r16, r17
mulsu r16, r17
fmul r16, r17
fmuls r16, r17
fmulsu r16, r17

inc r16
brne 1b
inc r17
brne 1b
sleep

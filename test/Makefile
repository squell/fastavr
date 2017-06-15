.SUFFIXES: .hex .s .o .aout

.s.o:
	avr-as --mmcu=atmega2560 $< -o$@

.o.aout:
	avr-ld -mavr6 $< -o$@

.aout.hex:
	avr-objcopy -O ihex $< $@

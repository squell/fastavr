.SUFFIXES: .hex .s .o .elf

.c.elf:
	avr-gcc -mmcu=atmega2560 $< -o$@

.c.o:
	avr-gcc -c -mmcu=atmega2560 $< -o$@

.s.o:
	avr-as --mmcu=atmega2560 $< -o$@

.o.elf:
	avr-ld -mavr6 $< -o$@

.elf.hex:
	avr-objcopy -O ihex $< $@

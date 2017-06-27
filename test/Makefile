.SUFFIXES: .hex .s .o .elf

.c.elf:
	avr-gcc -mmcu=atmega328 $< -o$@

.c.o:
	avr-gcc -c -mmcu=atmega328 $< -o$@

.s.o:
	avr-as --mmcu=atmega328 $< -o$@

.o.elf:
	avr-ld $< -o$@

.elf.hex:
	avr-objcopy -O ihex $< $@

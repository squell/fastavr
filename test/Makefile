.SUFFIXES: .hex .s .o .elf

.s.o:
	avr-as --mmcu=atmega2560 $< -o$@

.o.elf:
	avr-ld -mavr6 $< -o$@

.elf.hex:
	avr-objcopy -O ihex $< $@

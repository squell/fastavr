.SUFFIXES: .hex .elf

test: hello.hex
	../tester hello.hex 2> /dev/null

clean:
	rm -f hello.hex hello.o hello.elf

.elf.hex:
	avr-objcopy -j .text -j .data -O ihex $< $@

.c.elf:
	avr-gcc -mmcu=atmega328 $< -o$@

# rudimentary makefile

SHELL = /bin/sh

CC ?= cc
AS ?= as

CFLAGS  = -O2 -m32
LDFLAGS = -m32 -pthread
ASFLAGS = --32

tester: ihexread.o avr_core_x86.o tester.o

clean:
	rm -f *.o tester

# the tinyTwofish known-answer-test sends the mcu in an infinite loop
# if an error is encountered; or it will stop with a sleep instruction
# (reporting last opcode 9588) -- we check this
selftest: tester tinyTwofish/2fish_avr.s
	make -B -C example | grep 'Hello, world!'
	make -B -C tinyTwofish ckat CHIP=atmega2560
	avr-objcopy -O ihex tinyTwofish/ckat twofish.hex
	./tester twofish.hex 2>&1 | grep '\[9588\]$$'

tinyTwofish/2fish_avr.s:
	git submodule update --init

tester.o: tester.c ihexread.h
ihexread.c: ihexread.h

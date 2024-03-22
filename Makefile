# rudimentary makefile

SHELL = /bin/sh

CC ?= cc
AS ?= as

CFLAGS  = -O2 -m32
LDFLAGS = -m32 -pthread
ASFLAGS = --32

tester: ihexread.o ihexwrite.o avr_core_x86.o tester.o makepty.o des.o

clean:
	rm -f *.o tester

selftest: tester example/hello.hex tinyTwofish/ckat.hex
	@echo "[34m--- Testing using direct execution[0m"
	@sleep 1
	make selftest-direct
	@echo "[34m--- Testing using PTY for communication[0m"
	@sleep 1
	make -j2 selftest-pty
	@echo "[34m--- Testing using AVRdude[0m"
	@sleep 1
	make -j2 selftest-avrdude
	@echo "[34m--- Testing using AVRdude(Optiboot)[0m"
	@sleep 1
	make -j2 selftest-avrdude-optiboot

# the tinyTwofish known-answer-test sends the mcu in an infinite loop
# if an error is encountered; or it will stop with a sleep instruction
# (reporting last opcode 9588) -- we check this
selftest-direct: tester example/hello.hex tinyTwofish/ckat.hex
	./tester example/hello.hex 2>/dev/null | grep -m1 'Hello, world!'
	{ ./tester tinyTwofish/ckat.hex | true; } 2>&1 | grep '\[9588\]$$'
	@echo "[1mSuccess![0m"

selftest-pty: tester example/hello.hex pty-hello example/hello.pty

selftest-avrdude: tester example/hello.hex avrdude-upload-hello ATmegaBOOT_168_atmega328_pro_8MHz.pty pty-hello

selftest-avrdude-optiboot: tester example/hello.hex avrdude-upload-hello optiboot_atmega328.pty pty-hello

%.pty: %.hex
	@echo "[1mstarting the mcu preloaded with $<[0m"
	./tester -pty:/tmp/fnord $< 2>/dev/null

check-pty:
	@sleep 0.1; test -e /tmp/fnord || (echo "[1muse make -j2 to perform this test![0m" && false)

pty-hello: check-pty
	@echo "[1mopening a serial line to the mcu[0m"
	grep -m1 'Hello, world!' < /tmp/fnord
	@echo "[1mSuccess! Use Ctrl+\\ to SIGQUIT.[0m"

avrdude-upload-hello: check-pty
	@echo "[1mflashing the simulated mcu using AVRdude[0m"
	avrdude -P /tmp/fnord -p atmega328p -c arduino -D -u -U example/hello.hex
	@sync

tester.o: tester.c ihexread.h
ihexread.c: ihexread.h

eeprom.hex:
	printf "%4096s" | tr ' ' '\3ff' > eeprom.hex
	avr-objcopy -I binary -O ihex eeprom.hex

example/hello.hex:
	make -B -C example hello.hex

tinyTwofish/2fish_avr.s:
	git submodule update --init

tinyTwofish/ckat.hex: tinyTwofish/2fish_avr.s
	make -B -C tinyTwofish ckat.hex CHIP=atmega2560


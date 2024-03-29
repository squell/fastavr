*fastavr*
=======

Developing AVR code usually involves a lot of simulation, by using programs such as:

* _simavr_, an excellent all-round option, http://github.com/buserror/simavr/
* _μsim_, aimed strictly at testing crypto protocols https://github.com/hberntsen/usim
 
fastavr is like these, but with the main design goals of raw speed and simplicity of use; as well as being able to run actual bootloaders.

Features
=========

* Supports all common AVR instructions (see below)
* Optional user-definable behaviour of all AVR I/O ports
* Interrupts and single-stepping
* Possibility of simulating components using multi-threading
* Much faster than a physical AVR

Example
=======

See the file `tester.c`; this reads an AVR program (in IHEX8 format) and executes it on a emulated Atmega2560, causing bytes
written to USART0 to be written to the console.  It also defines a watchdog timer that can be used to auto-reset/kill a program
that is in a run-away condition (as described in Atmel's datasheets). Also emulated are the programmable timers TIMER0 and
TIMER1, as well as EEPROM memory (for handling non-volatile data).

Building
========

To build this project, you will need to have support for developing 32-bit programs installed. E.g. by installing libc6-dev-i386 on Debian.

Once you have that, this is likely to work:
```
make
```

Configuration options are found in `avr_core_x86.s`.

A quick test can be performed by running `make selftest`. This will test running .hex files directly, as well as flashing
the simulated board with AVRdude.

Todo/Limitations
====
* TIMER0 and TIMER1 can be set to either track "real time" or track emulated clock cycles.
  In the latter case, TIMER0/1 will be "time accelerated" since the emulator is much faster than a physical chip (unles you slow it down yourself).
  If you want to perform more accurate cycle measurement using TIMER0, the latter is needed, but the Optiboot bootloader needs wall time.
  Enable `TIME_ACCELERATION` to get the second behaviour.
  + Other configurations are possible by playing around with the `instantiate_prescaler` invocations,
    but you need to understand the code better to do that.

* No support for Reduced Core AVR; cycle count not correct for XMEGA microcontrollers.
 
* Not all illegal opcodes will result in an error. Neither will out-of-bounds SRAM accesses.

* The core has not yet been ported to x86-64.

Acknowledgements
================

Writing this pet project couldn't have been possible without Michel Pollet's excellent _simavr_.

Without the μNaCl project, testing this simulator wouldn't have been possible as well:
http://munacl.cryptojedi.org/atmega.shtml

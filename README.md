*fastavr*
=======

Developing AVR code usually involves a lot of simulation, by using programs such as:

* _simavr_, an excellent all-round option, http://github.com/buserror/simavr/
* _μsim_, aimed strictly at testing crypto protocols https://github.com/hberntsen/usim
  
fastavr is like these, but with the main design goals of raw speed and simplicity of use.

Features
=========

* Most of the common AVR instructions (see below)
* Optional user-definable behaviour of all AVR I/O ports
* Interrupts and single-stepping
* Possibility of simulating components using multi-threading
* Much faster than a physical AVR

Example
=======

See the file `tester.c`; this reads an AVR program (in IHEX8 format) and executes it, causing bytes written to
USART0 to be written to the console. It also defines a watchdog timer that can be used to auto-reset/kill a program
that is in a run-away condition (as described in Atmel's datasheets).

Building
========

This is likely to work:
```
make
```

Configuration options are found in `avr_core_x86.s`.

A quick test can be performed by running `make selftest`.

Todo/Limitations
====
* The S flag is always stuck at V⊕O due to the way fastavr handles
the flags. This should only be noticeable when using SES/SEV/SEO/CLS/CLV/CLO
or writing SREG via I/O-space

* No self-programming ability (SPM instruction, EEPROM).

* XCH/LAC/LAT/LAS not yet implemented

* No support for Reduced Core AVR, XMEGA, or extensions like DES/AES; also,
  not all unsupported instructions will result in an error, just like in the real world.

* The core needs to be ported to x86-64

Acknowledgements
================

Writing this pet project couldn't have been possible without Michel Pollet's excellent _simavr_. 

Without the μNaCl project, testing this simulator wouldn't have been possible as well:
http://munacl.cryptojedi.org/atmega.shtml

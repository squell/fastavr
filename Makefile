# rudimentary makefile

SHELL = /bin/sh

CC ?= cc
AS ?= as

CFLAGS  = -O2 -m32
LDFLAGS = -m32 -pthread
ASFLAGS = --32

tester: ihexread.o avr_core_x86.o tester.o

tester.o: tester.c ihexread.h
ihexread.c: ihexread.h

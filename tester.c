#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "ihexread.h"

extern volatile unsigned long long avr_cycle;
extern volatile unsigned long avr_last_wdr;
extern volatile unsigned char avr_IO[];
extern volatile unsigned char avr_INT;
extern volatile unsigned long avr_INTR;

extern unsigned long avr_PC;
extern unsigned char avr_ADDR[];
extern unsigned short int avr_FLASH[];
extern unsigned short int avr_SP;
extern unsigned char avr_SREG;

static unsigned char eeprom[0x10000];
static size_t eeprom_nonvolatile;
static const char *eeprom_file;

static volatile enum { I_WDINT, I_WDRESET, I_RESET } INT_reason;

#define reset break

/* note: consider calling this at regular intervals from a thread? */
void eeprom_commit(void)
{
	if(eeprom_nonvolatile && ihex_write(eeprom_file, eeprom, eeprom_nonvolatile) != 0) {
		fprintf(stderr, "error writing %s\n", eeprom_file);
		exit(2);
	}
}

/* dump the emulation state */
void avr_debug(unsigned long ip)
{
	int i;
	fprintf(stderr, "%10lld: ", avr_cycle);
	for(i=0; i < 32; i++)
	fprintf(stderr, "%02x ", avr_ADDR[i+0x00]);
	fprintf(stderr, "SP=%04x, SREG=%02x, PC=%04lx [%04x]", avr_SP, avr_SREG, ip, avr_FLASH[ip]);
	fprintf(stderr, "\n");
	eeprom_commit();
}

/* a watchdog process; behaves mostly according to the datasheet,
   except that the timed sequence involving WDCE isn't implemented */

pthread_t wdt_thread;

#define MCUSR 0x34
#define WDTCR 0x21

enum wdtcr_bits {
	WDIF = 1<<7, WDIE = 1<<6, WDCE = 1<<4, WDE = 1<<3
};

void *watchdog(void *threadid)
{
	unsigned long last_wdr = 0;
	unsigned long timer = 0;
	fprintf(stderr, "%s\n", "starting watchdog");
	for(;;) {
		usleep(1024);
		unsigned char wdtcr = avr_IO[WDTCR];
		unsigned long cur = avr_last_wdr;
		unsigned long threshold = 2ul << ((wdtcr&0x20)/4 + (wdtcr&0x7)) % 10;
		if(cur == last_wdr && wdtcr&0x48 && ++timer > threshold) {
			timer = 0;
			if(wdtcr & WDIE) {
				wdtcr |= WDIF;
				if(wdtcr & WDE)
					wdtcr &=~WDIE;
				avr_IO[WDTCR] = wdtcr;
				timer = 0;
				INT_reason = I_WDINT;
				avr_INT = 1;
			} else if(wdtcr & WDE) {
				timer = last_wdr = 0;
				INT_reason = I_WDRESET;
				avr_INT = 1;
				while(avr_INT && !(avr_IO[MCUSR]&0x8)) /* force-quit the emulator */
					avr_IO[0x3F] = 0x80;
			}
		} else if(cur != last_wdr) {
			timer = 0;
			last_wdr = cur;
		}
	}
	return 0;
}

/* scale the cpu cycle count according to TCCRxB, and generate an overflow interrupt if demanded
   if the first argument is NULL, only compute the scaled count; tihs routine is used to easily
   (partially) implement counters/timers

   NOTE that interrupts will only be generated when a counter value is polled; this should suffice
   for creating counters; for a more realistic emulation, an extra thread would be needed. */

#define GTCCR  0x23

static unsigned T_OVF_backlog; /* number of overflow events to catch up on */

static unsigned long long copy_timer(unsigned long long *prev, int tccr, int tifr, int timsk, int bits)
{
	tccr = avr_IO[tccr] & 7;
	if(!tccr) return 0;

	/* the prescaler is shared for all timers */
	static unsigned long long last_reset;
	static unsigned long long prev_cycle;
	static unsigned long long counted_cycle;
	if(avr_IO[GTCCR]&1) {
		if(last_reset != counted_cycle)
			last_reset = counted_cycle += avr_cycle - prev_cycle;
	} else {
		counted_cycle += avr_cycle - prev_cycle;
	}
	prev_cycle = avr_cycle;

	/* if prev != NULL, assume we are reading the register before the clock increases */
	int h = 1 - (tccr >> 2);
	int w = (2+h)*(tccr-h);
	unsigned long long scaled_count = counted_cycle - !!prev - (last_reset&(1<<w)-1) >> w;
	if(!prev) return scaled_count+(avr_IO[GTCCR]&1);

	if(*prev >> bits != scaled_count >> bits) {
		avr_IO[tifr] |= 1;
		if(avr_IO[timsk]&1) { /* generate interruptions */
			T_OVF_backlog = (scaled_count >> bits) - (*prev >> bits);
			INT_reason = tifr;
			avr_INT = 1;
		}
	}

	return *prev = scaled_count;
}

 /* we use I/O functions to
    - fake a UART: accept everything written to UDR0 (0xA6); always report ready on UCSR0A (0xA0)
    - implement EEPROM data accesses (note: timed sequence not implemented, nor interrupt-driven EEPROM)
    - implement 8-bit counter 0 and 16-bit counter 1; with interrupts, but no OCR/ICP */

#define UCSR0A 0xA0
#define UDR0   0xA6

#define TCNT0  0x26
#define TCCR0B 0x25
#define TIMSK0 0x4E
#define TIFR0  0x15

#define TCNT1L 0x64
#define TCNT1H 0x65
#define TCCR1B 0x61
#define TIMSK1 0x4F
#define TIFR1  0x16

#define EEARH  0x22
#define EEARL  0x21
#define EEDR   0x20
#define EECR   0x1F

/* the "temp" register to get 16-bit reads/writes */
static unsigned char TEMP;
/* offsets to derive the counters from the free-running prescaler */
static unsigned long long timer_ofs[2];

#define THREAD_IO 100
#ifdef THREAD_IO
pthread_t tty_thread;

volatile char uart_buffer[256];
volatile unsigned int uart_ptr, uart_end;

void *fake_console(void *threadid)
{
	avr_IO[UCSR0A] = 0x60;
	setbuf(stdout, 0);
	while(1) {
		int ptr = uart_ptr;
		while(ptr != uart_end || avr_IO[UCSR0A] == 0) {
			int c = uart_buffer[ptr];
			uart_ptr = ptr = (ptr+1) % sizeof uart_buffer;
			avr_IO[UCSR0A] = 0x60;
			if(c == 0x04) {
				fprintf(stderr, "end of transmission\n");
				avr_debug(0);
				exit(0);
			}
			putchar(c);
		}
		usleep(THREAD_IO);
	}
}
#endif

void avr_io_in(int port)
{
	switch(port) {
#ifndef THREAD_IO
	case UCSR0A:
		avr_IO[port] = 0x60;
		break;
#endif
	case TCNT0: {
		static unsigned long long timer;
		copy_timer(&timer, TCCR0B, TIFR0, TIMSK0, 8);
		avr_IO[port] = timer - timer_ofs[0];
		break;
	}
	case TCNT1L: {
		static unsigned long long timer;
		copy_timer(&timer, TCCR1B, TIFR1, TIMSK1, 16);
		avr_IO[port] = timer - timer_ofs[1];
		TEMP         = timer - timer_ofs[1] >> 8;
		break;
	}
	case TCNT1H:
		avr_IO[port] = TEMP;
		break;
	}
}

/* TODO: this should become a function "vetting" the data */
void avr_io_out(int port)
{
	switch(port) {
#ifdef THREAD_IO
		unsigned short cur;
	case UDR0:
		cur = uart_end;
		uart_buffer[cur] = avr_IO[port];
		cur = (cur+1) % sizeof uart_buffer;
		if(cur == uart_ptr) {
			avr_IO[UCSR0A] = 0x00;
			/* fprintf(stderr, "warning: flow control used\n"); */
		}
		uart_end = cur;
#else
		int c;
	case UDR0:
		c = avr_IO[port];
		if(c == 0x04) {
			fprintf(stderr, "end of transmission\n");
			avr_debug(0);
			exit(0);
		}
		putchar(c);
		fflush(stdout);
#endif
		break;
	case EECR:
		if((avr_IO[EECR]&6) == 6) { /* execute a write */
			avr_cycle += 2;
			if((avr_IO[port] & 0x20) == 0) /* check EEPM1 */
				eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]] = 0xFF;
			if((avr_IO[port] & 0x10) == 0) /* check EEPM0 */
				eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]] &= avr_IO[EEDR];
			avr_IO[port] &= ~7;
		} else if(avr_IO[EECR]&1) { /* execute a read */
			avr_cycle += 4;
			avr_IO[EEDR] = eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]];
			avr_IO[port] &= ~7;
		}
		break;

	case TIFR0:
	case TIFR1:
		avr_IO[port] = 0; /* any write clears the flag */
		break;
	case TCNT0:
		timer_ofs[0] = copy_timer(NULL, TCCR0B, 0,0,0);
		timer_ofs[0] -= (timer_ofs[0] & ~0xFF | avr_IO[port]);
		break;
	case TCNT1L:
		timer_ofs[1] = copy_timer(NULL, TCCR1B, 0,0,0);
		timer_ofs[1] -= (timer_ofs[1] & ~0xFFFF | TEMP<<8 | avr_IO[port]);
		break;
	case TCNT1H:
		TEMP = avr_IO[port];
		break;
	case GTCCR:
		copy_timer(NULL, 1, 0,0,0); /* resets the prescaler if demanded */
		if(!(avr_IO[port]&0x80)) avr_IO[port] = 0;
		break;
	}
}

static void ctrlC_handler(int sig)
{
	INT_reason = I_RESET;
	avr_IO[0x3F] = 0x80; /* not as good as the watchdog force-quit */
	avr_INT = 1;
	static int count;    /* fallback */
	if(avr_INT && count++ >= 16) abort();
}

int main(int argc, char **argv)
{
	memset(avr_FLASH, 0xFF, 0x40000);
	if(!argv[1]) {
		fprintf(stderr, "usage: tester flash.hex [eeprom.hex]]\n");
		return 2;
	}
	int n = ihex_read(argv[1], avr_FLASH, 0x40000);
	if(n < 0)  {
		fprintf(stderr, "could not read %s\n", argv[1]);
		return 2;
	}
	fprintf(stderr, "%d bytes read\n", n);

	memset(eeprom, 0xFF, sizeof eeprom);
	if(argv[2]) {
		eeprom_nonvolatile = ihex_read(eeprom_file=argv[2], eeprom, sizeof eeprom);
		if(eeprom_nonvolatile < 0) {
			fprintf(stderr, "could not read %s\n", argv[2]);
			return 2;
		}
	    fprintf(stderr, "%d bytes nonvolatile eeprom\n", eeprom_nonvolatile);
	}

	signal(SIGINT, ctrlC_handler);
	avr_reset();
	avr_IO[MCUSR] |= 0x1;  /* set PORF */
	/* avr_IO[WDTCR] |= WDE; uncomment this to start the watchdog timer by default */
	pthread_create(&wdt_thread, 0, watchdog, 0);
#ifdef THREAD_IO
	pthread_create(&tty_thread, 0, fake_console, 0);
#endif
	do {
		switch( avr_run() ) {
		case 0:
			switch(INT_reason) {
			case I_WDINT:
				fprintf(stderr, "%s\n", "watchdog interrupt");
				/* avr_PC = 0xC; on ATtiny */
				avr_PC = 0x18;
				avr_IO[WDTCR] &=~WDIF;
				continue;
			case I_WDRESET:
				fprintf(stderr, "%s\n", "watchdog reset");
				avr_reset();
				avr_IO[MCUSR] |= 0x8; /* set WDRF */
				reset;
			case I_RESET:
				fprintf(stderr, "%s\n", "external reset");
				avr_reset();
				avr_IO[MCUSR] |= 0x2; /* set EXTRF */
				reset;
			case TIFR0:
				avr_IO[TIFR0] &= ~1;
				avr_PC = 0x2E;
				if(T_OVF_backlog--) avr_INT = 1;
				continue;
			case TIFR1:
				avr_IO[TIFR1] &= ~1;
				avr_PC = 0x28;
				if(T_OVF_backlog--) avr_INT = 1;
				continue;
			}
			break;
		case 1:
			/* TODO: allow resuming execution on WDT (or possible USART) interrupts */
			fprintf(stderr, "%s\n", "mcu idle");
			break;
		case 2:
			fprintf(stderr, "%s\n", "breakpoint");
			do {
				avr_debug(avr_PC);
				getchar();
			} while(avr_step() == 0);
			break;
		case 3:
			fprintf(stderr, "%s\n", "mcu spinlocked");
			break;
		default:
			fprintf(stderr, "unexpected situation: PC=%04lx instruction=%04x\n", avr_PC-1, avr_FLASH[avr_PC-1]);
			break;
		}
		break;
	} while(1);
#ifdef THREAD_IO
	while(uart_ptr != uart_end) pthread_yield();
#endif
	fprintf(stderr, "%s\n", "done");
	avr_debug(avr_PC-1);
	return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
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

static volatile enum { I_WDINT, I_WDRESET } INT_reason;

/* dump the emulation state */
void avr_debug(unsigned long ip)
{
	int i;
	fprintf(stderr, "%10lld: ", avr_cycle);
	for(i=0; i < 32; i++)
	fprintf(stderr, "%02x ", avr_ADDR[i+0x00]);
	fprintf(stderr, "SP=%04x, SREG=%02x, PC=%04lx [%04x]", avr_SP, avr_SREG, ip, avr_FLASH[ip]);
	fprintf(stderr, "\n");
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
				timer = 0;
				INT_reason = I_WDRESET;
				avr_INT = 1;
				while(avr_INT)  /* force-quit the emulator */
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

static unsigned long long prescaler;
static unsigned T_OVF_backlog; /* number of overflow events to catch up on */

static unsigned long long copy_timer(unsigned long long *prev, int tccr, int tifr, int timsk, int bits)
{
	tccr = avr_IO[tccr] & 7;
	if(!tccr) return 0;

	if(avr_IO[GTCCR]&1) prescaler = avr_cycle;

	/* if prev != NULL, assume we are reading the register before the clock increases */
	int h = 1 - (tccr >> 2);
	unsigned long long scaled_count = avr_cycle -!!prev - prescaler >> (2+h)*(tccr-h);
	if(!prev) return scaled_count;

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

/* the "temp" register to get 16-bit reads/writes */
static unsigned char TEMP;
/* offsets to derive the counters from the free-running prescaler */
static unsigned long long timer_ofs[2];

void avr_io_in(int port)
{
	switch(port) {
	case UCSR0A:
		avr_IO[port] = -1;
		break;
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

int main(int argc, char **argv)
{
	memset(avr_FLASH, 0xFF, 0x40000);
	if(!argv[1]) {
		fprintf(stderr, "usage: tester [file.hex]\n");
		return 2;
	}
	int n = ihex_read(argv[1], avr_FLASH, 0x40000);
	if(n < 0)  {
		fprintf(stderr, "could not read %s\n", argv[1]);
		return 2;
	}
	fprintf(stderr, "%d bytes read\n", n);

	avr_reset();
	avr_IO[MCUSR] |= 0x1;  /* set PORF */
	/* avr_IO[WDTCR] |= WDE; uncomment this to start the watchdog timer by default */
	pthread_create(&wdt_thread, 0, watchdog, 0);
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
				break;
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
	fprintf(stderr, "%s\n", "done");
	avr_debug(avr_PC-1);
	return 0;
}

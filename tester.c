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

/* a watchdog process; behaves mostly according to the datasheet,
   except that the timed sequence involving WDCE isn't implemented */

pthread_t wdt_thread;

#define MCUSR (avr_IO[0x34])
#define WDTCR (avr_IO[0x21])

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
		unsigned char wdtcr = WDTCR;
		unsigned long cur = avr_last_wdr;
		unsigned long threshold = 2ul << ((wdtcr&0x20)/4 + (wdtcr&0x7)) % 10;
		if(cur == last_wdr && wdtcr&0x48 && ++timer > threshold) {
			timer = 0;
			if(wdtcr & WDIE) {
				wdtcr |= WDIF;
				if(wdtcr & WDE)
					wdtcr &=~WDIE;
				WDTCR = wdtcr;
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

 /* two io functions to fake a proper USART */

void avr_io_in(int port)
{
	if(port == 0xA0)
		avr_IO[port] = -1;
}

void avr_io_out(int port)
{
	switch(port) {
		int c;
	case 0xA6:
		c = avr_IO[port];
		if(c == 0x04) {
			fprintf(stderr, "end of transmission\n");
			avr_debug(0);
			exit(0);
		}
		putchar(c);
		fflush(stdout);
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
	MCUSR |= 0x1;  /* set PORF */
	/* WDTCR |= WDE; uncomment this to start the watchdog timer by default */
	pthread_create(&wdt_thread, 0, watchdog, 0);
	do {
		switch( avr_run() ) {
		case 0:
			switch(INT_reason) {
			case I_WDINT:
				fprintf(stderr, "%s\n", "watchdog interrupt");
				avr_PC = 0xC;
				WDTCR &=~WDIF;
				continue;
			case I_WDRESET:
				fprintf(stderr, "%s\n", "watchdog reset");
				avr_reset();
				MCUSR |= 0x8; /* set WDRF */
				break;
			}
			break;
		case 1:
			fprintf(stderr, "%s\n", "mcu idle");
			break;
		case 2:
			fprintf(stderr, "%s\n", "breakpoint");
			do {
			    avr_debug(avr_PC-1);
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

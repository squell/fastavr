#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include "ihexread.h"

/* #define THREAD_IO 10 */
/* #define THREAD_TIMER 1000 */

extern volatile unsigned long long avr_cycle;
extern volatile unsigned long avr_last_wdr;
extern volatile unsigned char avr_IO[];
extern volatile unsigned char avr_INT;
extern volatile unsigned long avr_INTR;

extern unsigned long avr_PC;
extern unsigned char avr_ADDR[];
extern unsigned short int avr_FLASH[];
extern unsigned short int avr_SP;
extern unsigned char volatile avr_SREG;

int avr_run();
int avr_step();
void avr_reset();

static unsigned char eeprom[0x10000];
static size_t eeprom_nonvolatile;
static const char *eeprom_file;

static volatile enum { INTR, WDRESET, XRESET } INT_reason;

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

/* usleep is deprecated in POSIX */
#define usleep(us) \
	{ struct timespec ts; \
	  clock_gettime(CLOCK_MONOTONIC, &ts); \
	  ts.tv_nsec += us*1000; \
	  ts.tv_nsec %= 1000000; \
	  ts.tv_sec += (ts.tv_nsec + us*1000)/1000000; \
	  while(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)); }
#define ualarm(us,iv) \
	{ const struct timeval tv  = { us/1000000, us%1000000 }; \
	  const struct itimerval itv = { tv, tv }; \
	  setitimer(ITIMER_REAL, &itv, NULL); }

/* a watchdog process; behaves mostly according to the datasheet. */

#define MCUSR 0x34
#define WDTCR 0x21

enum wdtcr_bits {
	WDIF = 1<<7, WDIE = 1<<6, WDCE = 1<<4, WDE = 1<<3
};

enum mcusr_bits {
	WDRF = 1<<3, EXTRF = 1<<1, PORF = 1<<0
};

static pthread_t wdt_thread;

static void *watchdog(void *threadid)
{
	unsigned long last_wdr = 0;
	unsigned long timer = 0;
	fprintf(stderr, "%s\n", "starting watchdog");
	for(;;) {
		unsigned char wdtcr = avr_IO[WDTCR];
		unsigned long cur = avr_last_wdr;
		unsigned long threshold = 2ul << ((wdtcr&0x20)/4 + (wdtcr&0x7)) % 10;
		if(cur == last_wdr && wdtcr&(WDIE|WDE) && ++timer > threshold) {
			timer = 0;
			if(wdtcr & WDIE) {
				wdtcr |= WDIF;
				if(wdtcr & WDE)
					wdtcr &=~WDIE;
				avr_IO[WDTCR] = wdtcr;
				timer = 0;
				avr_INT = 1;
			} else if(wdtcr & WDE) {
				timer = last_wdr = 0;
				INT_reason = WDRESET;
				avr_INT = 1;
				while(avr_INT && !(avr_IO[MCUSR]&WDRF)) /* force-quit the emulator */
					avr_SREG = 0x80;
			}
		} else if(cur != last_wdr) {
			timer = 0;
			last_wdr = cur;
		}
		usleep(1024);
	}
	return NULL;
}

/* scale the cpu cycle count according to TCCRxB, and generate an overflow interrupt if demanded
   if the first argument is NULL, only compute the scaled count; tihs routine is used to easily
   (partially) implement counters/timers

   NOTE that interrupts will only be generated when a counter value is polled; this should suffice
   for creating counters; for a more realistic emulation, an extra thread would be needed. */

#define GTCCR  0x23

enum timer_bits {
	TSM = 1<<7, PSRSYNC = 1<<0, /* GTCCR */
	TOV = 1<<0                  /* TIMSKn & TIFRn */
};

static void simulate_timer(unsigned long long *prev, int tccr, int tifr, int timsk, int bits, int offset, volatile unsigned *overflow_events)
{
	/* the prescaler is shared for all timers */
	static unsigned long long last_reset;
	static unsigned long long prev_cycle;
	static unsigned long long counted_cycle;

	tccr = avr_IO[tccr] & 7;
	if(!tccr) return;

	if(avr_IO[GTCCR]&PSRSYNC) {
		if(last_reset != counted_cycle)
			last_reset = counted_cycle += avr_cycle - prev_cycle;
	} else {
		counted_cycle += avr_cycle - prev_cycle;
	}
	prev_cycle = avr_cycle;

	if(prev) {
		/* assume we are reading the register before the clock increases */
		int h = 1 - (tccr >> 2);
		int w = (2+h)*(tccr-h);
		unsigned long long scaled_count = counted_cycle-1 - (last_reset&(1<<w)-1) >> w;
		scaled_count += offset;
		if(*prev >> bits != scaled_count >> bits) {
			avr_IO[tifr] |= TOV;
			if(avr_IO[timsk]&TOV) { /* generate overflow interruptions */
				*overflow_events = (scaled_count >> bits) - (*prev >> bits);
				avr_INT = 1;
			}
		}
		*prev = scaled_count;
	}
}

 /* we use I/O functions to
    - fake a UART: accept everything written to UDR0 (0xA6); always report ready on UCSR0A (0xA0)
    - implement EEPROM data accesses
    - implement 8-bit counter 0 and 16-bit counter 1; with interrupts, but no OCR/ICP */

#define UCSR0A 0xA0
#define UCSR0B 0xA1
#define UDR0   0xA6

enum ucsr_bits {
	RXC = 1<<7, TXC = 1<<6, UDRE = 1<<5
};

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

#define vec_WDIF 0x18
#define vec_TOV0 0x2E
#define vec_TOV1 0x28
#define vec_EERI 0x3C
#define vec_UDRE 0x34
#define vec_TXC  0x36
#define vec_RXC  0x32

enum eecr_bits {
	EEPM1 = 1<<5, EEPM0 = 1<<4, EERIE = 1<<3, EEMPE = 1<<2, EEPE = 1<<1, EERE = 1<<0
};

/* the "temp" register to get 16-bit reads/writes */
static unsigned char TEMP;
/* offsets to derive the counters from the free-running prescaler */
static unsigned long long timer[2];
static int timer_ofs[2];
/* number of overflow events to catch up on */
static volatile unsigned timer_overflows[2];

#define fetch_timer(n) \
	simulate_timer(&timer[n], TCCR##n##B, TIFR##n, TIMSK##n, n==1? 16 : 8, timer_ofs[n], &timer_overflows[n])

#define set_timer(n, val) \
	fetch_timer(n); \
	timer[n] -= timer_ofs[n]; \
	timer[n] += timer_ofs[n] = (val) - (timer[n]&(n==1? 0xFFFF: 0xFF));


/* a routine for asynchronously polling the timer */
static void timer_poll_handler(int sig)
{
	fetch_timer(0);
	fetch_timer(1);
}

#define OR(x,y) __sync_fetch_and_or(&x,y)
#define AND(x,y) __sync_fetch_and_and(&x,y)
#define INCR(x) __sync_add_and_fetch(&x,1)
#define DECR(x) __sync_fetch_and_sub(&x,1)

#ifdef THREAD_IO
static pthread_t tty_thread;

static volatile unsigned char uart_buffer[256];
static volatile unsigned int uart_num;

static void *fake_console(void *threadid)
{
	int ptr = 0;
	while(1) {
		while(uart_num > 0) {
			int c = uart_buffer[ptr], old_ucsr;
			ptr = (ptr+1) % sizeof uart_buffer;
			old_ucsr = OR(avr_IO[UCSR0A], TXC|UDRE);
			DECR(uart_num);
#ifndef DELAY_IO
			if((old_ucsr & (TXC|UDRE)) == 0 && avr_IO[UCSR0B] & (TXC|UDRE))
				avr_INT = 1;
#else
			if(avr_IO[UCSR0B] & (TXC|UDRE))
				avr_INT = 1;
#endif
			if(c == 0x04) {
				fprintf(stderr, "end of transmission\n");
				avr_debug(0);
				exit(0);
			}
			putchar(c);
#ifdef BAUD
			usleep(10000000/BAUD);
#endif
		}
		usleep(THREAD_IO);
	}
}

static pthread_t rbr_thread;

static volatile unsigned char rdbr_buffer[256];
static volatile unsigned int rdbr_num = sizeof rdbr_buffer;

static void *fake_receiver(void *threadid)
{
	int ptr = 0;
	sched_yield();
	while(1) {
		int c = getchar(), old_ucsr;
		if(c != EOF) {
			rdbr_buffer[ptr] = c;
			ptr = (ptr+1) % sizeof rdbr_buffer;
			old_ucsr = OR(avr_IO[UCSR0A], RXC);
			DECR(rdbr_num);
		} else if(rdbr_num == sizeof rdbr_buffer) {
			break;
		} else
			old_ucsr = OR(avr_IO[UCSR0A], RXC);
#ifndef DELAY_IO
		if(!(old_ucsr & RXC) && avr_IO[UCSR0B] & RXC)
			avr_INT = 1;
#else
		if(avr_IO[UCSR0B] & RXC)
			avr_INT = 1;
#endif
#ifdef BAUD
		usleep(10000000/BAUD);
#endif
		while(rdbr_num == 0)
			usleep(THREAD_IO);
	}
	return NULL;
}
#endif

/* signal handler to handle arrival of data */
static void io_input_handler(int sig)
{
	if((avr_IO[UCSR0A] & RXC) == 0) {
		avr_INT = 1;
	}
}

void avr_io_in(int port)
{
	switch(port) {
#ifdef THREAD_IO
		static int cur = 0;
	case UDR0:
		avr_IO[port] = rdbr_buffer[cur];
		cur = (cur+1) % sizeof rdbr_buffer;
		AND(avr_IO[UCSR0A], ~RXC);
		if(INCR(rdbr_num) < sizeof rdbr_buffer) {
#  ifndef DELAY_IO
			OR(avr_IO[UCSR0A], RXC);
			if(avr_IO[UCSR0B] & RXC)
				avr_INT = 1;
#  endif
		} else {
			/* fprintf(stderr, "warning: flow control used\n"); */
		}
#else
		int c;
	case UDR0:
		avr_IO[port] = getchar();
		AND(avr_IO[UCSR0A], ~RXC);
	case UCSR0A:
		if((avr_IO[UCSR0A] & RXC) == 0 && (c=getchar()) != EOF) {
			OR(avr_IO[UCSR0A], RXC), ungetc(c, stdin);
			if(avr_IO[UCSR0B] & RXC)
				avr_INT = 1;
		}
#endif
		break;
	case TCNT0:
		fetch_timer(0);
		avr_IO[port] = timer[0];
		break;
	case TCNT1L:
		fetch_timer(1);
		avr_IO[port] = timer[1];
		TEMP         = timer[1] >> 8;
		break;
	case TCNT1H:
		avr_IO[port] = TEMP;
		break;
	}
}

void avr_io_out(int port, unsigned char prev)
{
	switch(port) {
		static unsigned long long last_wdce = -4;
		static unsigned long long last_eempe = -4;
#ifdef THREAD_IO
		static int cur = 0;
	case UDR0:
		uart_buffer[cur] = avr_IO[port];
		cur = (cur+1) % sizeof uart_buffer;
		AND(avr_IO[UCSR0A], ~(TXC|UDRE));
		if(INCR(uart_num) < sizeof uart_buffer) {
#  ifndef DELAY_IO
			OR(avr_IO[UCSR0A], TXC|UDRE);
			if(avr_IO[UCSR0B] & (TXC|UDRE))
				avr_INT = 1;
#  endif
		} else {
			/* fprintf(stderr, "warning: flow control used\n"); */
		}
		break;
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
		OR(avr_IO[UCSR0A], TXC|UDRE);
		if(avr_IO[UCSR0B] & TXC|UDRE)
			avr_INT = 1;
		break;
#endif
	case UCSR0A:
		/* only allow writing the R/W parts */
		avr_IO[port] = prev&~0x43 | (avr_IO[port]&0x43 | ~prev&TXC) ^ TXC;
		break;
	case UCSR0B:
		avr_io_in(UCSR0A);
		if(avr_IO[UCSR0A] & avr_IO[port] & (RXC|UDRE))
			avr_INT = 1;
		break;

	case EECR:
		if(avr_cycle-last_eempe <= 4 && avr_IO[port]&EEPE) { /* execute a write */
			avr_cycle += 2;
			if((avr_IO[port] & EEPM1) == 0)
				eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]] = 0xFF;
			if((avr_IO[port] & EEPM0) == 0)
				eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]] &= avr_IO[EEDR];
			avr_IO[port] &= ~(EEMPE|EEPE|EERE);
		} else if(avr_IO[port]&EERE) { /* execute a read */
			avr_cycle += 4;
			avr_IO[EEDR] = eeprom[avr_IO[EEARH]<<8 | avr_IO[EEARL]];
			avr_IO[port] &= ~(EEMPE|EEPE|EERE);
		}
		if(avr_IO[port] & EEMPE)
			last_eempe = avr_cycle;
		if(avr_IO[port] & EERIE)
			avr_INT = 1;
		break;

	case TIFR0:
	case TIFR1:
		avr_IO[port] = 0; /* any write clears the flag */
		break;
	case TCNT0:
		avr_cycle++;
		set_timer(0, avr_IO[TCNT0]);
		avr_cycle--;
		break;
	case TCNT1L:
		avr_cycle++;
		set_timer(1, avr_IO[TCNT1L]+TEMP*0x100);
		avr_cycle--;
		break;
	case TCNT1H:
		TEMP = avr_IO[TCNT1H];
		break;
	case TCCR0B:
		set_timer(0, avr_IO[TCNT0]);
		break;
	case TCCR1B: /* duplicate code to avoid re-use of TEMP */
		set_timer(1, avr_IO[TCNT1L]+avr_IO[TCNT1H]*0x100);
		break;
	case GTCCR:
		if(!(avr_IO[port]&PSRSYNC)) avr_IO[port] = 0; /* TSM=1/PSRSYNC=0 ?? */
		avr_IO[port] |= prev&PSRSYNC;
		simulate_timer(NULL, GTCCR, 0,0,0, 0, NULL); /* resets the prescaler if demanded */
		if(!(avr_IO[port]&TSM))
			avr_IO[port] = 0;
		break;
	case WDTCR:
		if(avr_cycle-last_wdce > 4 || avr_IO[MCUSR]&WDRF) {
			avr_IO[port] = prev&0x2F | avr_IO[port]&~0x27;
		}
		if(avr_IO[port]&(WDCE|WDE))
			last_wdce = avr_cycle;
		avr_IO[port] &= ~(WDCE | avr_IO[port]&WDIF);
		break;
	}
}

static void ctrlC_handler(int sig)
{
	static int count;    /* fallback */
	INT_reason = XRESET;
	avr_SREG = 0x80;     /* not as good as the watchdog force-quit */
	avr_INT = 1;
	if(avr_INT && count++ >= 16) abort();
}

static struct termios stdin_termios;
static void restore_stdin(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &stdin_termios);
}

int main(int argc, char **argv)
{
	memset(avr_FLASH, 0xFF, 0x40000);
	if(!argv[1]) {
		fprintf(stderr, "usage: tester flash.hex [eeprom.hex]]\n");
		return 2;
	} else {
		int n = ihex_read(argv[1], avr_FLASH, 0x40000);
		if(n < 0)  {
			fprintf(stderr, "could not read %s\n", argv[1]);
			return 2;
		}
		fprintf(stderr, "%d bytes read\n", n);
	}

	memset(eeprom, 0xFF, sizeof eeprom);
	if(argv[2]) {
		int n = ihex_read(eeprom_file=argv[2], eeprom, sizeof eeprom);
		if(n < 0) {
			fprintf(stderr, "could not read %s\n", argv[2]);
			return 2;
		}
		fprintf(stderr, "%d bytes nonvolatile eeprom\n", n);
		eeprom_nonvolatile = n;
	}

	signal(SIGINT, ctrlC_handler);
	if(isatty(STDOUT_FILENO)) setbuf(stdout, NULL);

	if(isatty(STDIN_FILENO)) {
		struct termios ctrl;
		tcgetattr(STDIN_FILENO, &ctrl);
		stdin_termios = ctrl;
		atexit(restore_stdin);
		ctrl.c_lflag &= ~ICANON; /* make stdin unbuffered */
		tcsetattr(STDIN_FILENO, TCSANOW, &ctrl);
	}

	avr_reset();
	avr_IO[MCUSR] |= PORF;
	avr_IO[UCSR0A] = UDRE;
	/* avr_IO[WDTCR] |= WDE; uncomment this to start the watchdog timer by default */
	pthread_create(&wdt_thread, NULL, watchdog, NULL);
#ifdef THREAD_IO
	pthread_create(&tty_thread, NULL, fake_console, NULL);
	pthread_create(&rbr_thread, NULL, fake_receiver, NULL);
#else
	signal(SIGIO, io_input_handler);
	fcntl(STDIN_FILENO, F_SETOWN, getpid());
	fcntl(STDIN_FILENO, F_SETFL, O_RDONLY | O_ASYNC | O_NONBLOCK);
#endif
#ifdef THREAD_TIMER
	{
		/* block the main CPU thread from getting bogged down with the timer */
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGALRM);
		pthread_sigmask(SIG_BLOCK, &set, NULL);
	}
	signal(SIGALRM, timer_poll_handler);
	ualarm(THREAD_TIMER,0);
#endif
	do {
		switch( avr_run() ) {
		case 0:
			if(INT_reason == XRESET) {
				fprintf(stderr, "%s\n", "external reset");
				avr_reset();
				avr_IO[MCUSR] |= EXTRF;
				reset;
			} else if(INT_reason == WDRESET) {
				fprintf(stderr, "%s\n", "watchdog reset");
				avr_reset();
				avr_IO[MCUSR] |= WDRF;
				reset;
			} else if(avr_IO[WDTCR] & WDIF) {
				fprintf(stderr, "%s\n", "watchdog interrupt");
				avr_PC = vec_WDIF;
				avr_IO[WDTCR] &=~WDIF;
				continue;
			} else if(timer_overflows[0]) {
				avr_IO[TIFR0] &= ~TOV;
				avr_PC = vec_TOV0;
				if(--timer_overflows[0]) avr_INT = 1;
				continue;
			} else if(timer_overflows[1]) {
				avr_IO[TIFR1] &= ~TOV;
				avr_PC = vec_TOV1;
				if(--timer_overflows[1]) avr_INT = 1;
				continue;
			} else if(avr_IO[EECR] & EERIE) {
				avr_INT = 1; /* always see if EERIE is resolved */
				avr_PC = vec_EERI;
				continue;
			} else { /* must be a serial-related interrupt... */
				avr_io_in(UCSR0A);
				switch(avr_IO[UCSR0B] & avr_IO[UCSR0A] & (TXC|UDRE)) {
				case UDRE: /* UDR empty - do not clear flag */
				case TXC|UDRE:
					avr_INT = 1; /* always see if UDR is resolved */
					avr_PC = vec_UDRE;
					continue;
				case TXC:  /* TX complete - clear flag, set UDRE */
					avr_PC = vec_TXC;
					AND(avr_IO[UCSR0A], ~TXC);
					continue;
				default:
					if(avr_IO[UCSR0B] & avr_IO[UCSR0A] & RXC) {
						avr_INT = 1; /* always check if RXC is resolved */
						avr_PC = vec_RXC;
						continue;
					}
					goto ignore;
				};
			ignore: /* everything ok, perform an IRET (kind of kludgy) */
				avr_SREG |= 0x80;
				avr_PC  = avr_ADDR[++avr_SP] << 16;
				avr_PC |= avr_ADDR[++avr_SP] << 8;
				avr_PC |= avr_ADDR[++avr_SP];
				avr_cycle -= 5;
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
				//getchar();
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
	while(uart_num) sched_yield();
#endif
	fprintf(stderr, "%s\n", "done");
	avr_debug(avr_PC-1);
	return 0;
}

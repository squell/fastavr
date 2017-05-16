#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include "ihexread.h"

/* #define THREAD_IO 10 */
#define THREAD_TIMER 100
#define WD_FREQ 128000/64

/* should the emulator quit if the only thing that will get things moving again is a reset? */
/* #define HALT_QUIT */

extern volatile unsigned long long avr_cycle;
extern volatile unsigned long avr_last_wdr;
extern volatile unsigned char avr_IO[];
extern volatile unsigned char avr_INT;
extern volatile unsigned long avr_INTR;

extern unsigned long avr_PC, avr_BOOT_PC;
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

static volatile enum { INTR, WDRESET, XRESET, POWEROFF } INT_reason;

#define reset { INT_reason = INTR; do; while(kill_with_fire); continue; }

/* note: consider calling this at regular intervals from a thread? */
void eeprom_commit(void)
{
	if(eeprom_nonvolatile && ihex_write(eeprom_file, eeprom, eeprom_nonvolatile, 0) != 0) {
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
	{ const struct timespec ts = { (us)/1000000, ((us)%1000000)*1000 }; nanosleep(&ts, NULL); }
#define ualarm(us,iv) \
	{ const struct timeval u_tv = { (us)/1000000, (us)%1000000 }; \
	  const struct timeval i_tv = { (iv)/1000000, (iv)%1000000 }; \
	  const struct itimerval itv = { i_tv, u_tv }; \
	  setitimer(ITIMER_REAL, &itv, NULL); }
#define uvalarm(us,iv) \
	{ const struct timeval u_tv = { (us)/1000000, (us)%1000000 }; \
	  const struct timeval i_tv = { (iv)/1000000, (iv)%1000000 }; \
	  const struct itimerval itv = { i_tv, u_tv }; \
	  setitimer(ITIMER_VIRTUAL, &itv, NULL); }

/* a watchdog process; behaves mostly according to the datasheet. */

#define MCUSR  0x34
#define WDTCSR 0x40

enum wdtcr_bits {
	WDIF = 1<<7, WDIE = 1<<6, WDCE = 1<<4, WDE = 1<<3
};

enum mcusr_bits {
	WDRF = 1<<3, BORF = 1<<2, EXTRF = 1<<1, PORF = 1<<0
};

#ifndef WD_FREQ
#define WD_FREQ 128000
#endif

void watchdog(int alarm)
{
	static unsigned long last_wdr;
	static unsigned long timer;

	unsigned char wdtcr = avr_IO[WDTCSR];
	unsigned long cur = avr_last_wdr;
	unsigned long threshold = 2ul << ((wdtcr&0x20)/4 + (wdtcr&0x7)) % 10;
	if(cur == last_wdr && wdtcr&(WDIE|WDE) && ++timer > threshold) {
		timer = 0;
		if(wdtcr & WDIE) {
			wdtcr |= WDIF;
			if(wdtcr & WDE)
				wdtcr &=~WDIE;
			avr_IO[WDTCSR] = wdtcr;
			timer = 0;
			avr_INT = 1;
		} else if(wdtcr & WDE) {
			timer = last_wdr = 0;
			INT_reason = WDRESET;
			avr_INT = 1;
			avr_SREG = 0x80;
		}
	} else if(cur != last_wdr) {
		timer = 0;
		last_wdr = cur;
	}
}

/* scale the cpu cycle count according to TCCRxB, and generate an overflow interrupt if demanded
   if the first argument is NULL, only compute the scaled count; tihs routine is used to easily
   (partially) implement counters/timers

   NOTE that interrupts will only be generated when a counter value is polled; this should suffice
   for creating counters; for a more realistic emulation, an extra thread would be needed. */

#define GTCCR  0x23

enum timer_bits {
	TSM = 1<<7, PSRASY = 1<<1, PSRSYNC = 1<<0, /* GTCCR */
	TOV = 1<<0,                                /* TIMSKn & TIFRn */
	AS2 = 1<<5                                 /* ASSR */
};

#define instantiate_prescaler(simulated_timer, reset, t1, t2, t3, t4, t5, t6, t7, clock_src) \
static void simulated_timer(unsigned long long *prev, int tccr, int tifr, int timsk, int bits, int offset, volatile unsigned *overflow_events) \
{ \
	/* the prescaler is shared for all timers */ \
	static unsigned long long last_reset; \
	static unsigned long long prev_cycle; \
	static unsigned long long counted_cycle; \
	static volatile char busy = 0; /* this function is not re-entrant*/ \
	unsigned long long cycle = clock_src; \
 \
	tccr = avr_IO[tccr] & 7; \
	if(!tccr || busy) return; \
	busy++; \
 \
	if(reset) { \
		if(last_reset != counted_cycle) \
			last_reset = counted_cycle += cycle - prev_cycle; \
	} else { \
		counted_cycle += cycle - prev_cycle; \
	} \
	prev_cycle = cycle; \
 \
	if(prev) { \
		/* assume we are reading the register before the clock increases */ \
		const int tap[7] = { t1,t2,t3,t4,t5,t6,t7 }; \
		const int w = tap[tccr-1]; \
		unsigned long long scaled_count = counted_cycle-1 - (last_reset&(1<<w)-1) >> w; \
		scaled_count += offset; \
		if(*prev >> bits != scaled_count >> bits) { \
			avr_IO[tifr] |= TOV; \
			if(avr_IO[timsk]&TOV) { /* generate overflow interruptions */ \
				*overflow_events = (scaled_count >> bits) - (*prev >> bits); \
				avr_INT = 1; \
			} \
		} \
		*prev = scaled_count; \
	} \
	busy--; \
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

#define TCNT2  0x92
#define TCCR2B 0x91
#define TIMSK2 0x50
#define TIFR2  0x17
#define ASSR   0x96

#define EEARH  0x22
#define EEARL  0x21
#define EEDR   0x20
#define EECR   0x1F

#define vec_WDIF 0x18
#define vec_TOV0 0x2E
#define vec_TOV1 0x28
#define vec_TOV2 0x1E
#define vec_EERI 0x3C
#define vec_UDRE 0x34
#define vec_TXC  0x36
#define vec_RXC  0x32

enum eecr_bits {
	EEPM1 = 1<<5, EEPM0 = 1<<4, EERIE = 1<<3, EEMPE = 1<<2, EEPE = 1<<1, EERE = 1<<0
};

static unsigned long long oscillator(unsigned long long freq)
{
	struct timespec ts = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec*freq + ts.tv_nsec*freq / 1000000000;
}

/* let timer0 and timer1 fake a 16mhz unit -- does mean that they cannot be used anymore for cycle measurement */
instantiate_prescaler(PRESCALER01, avr_IO[GTCCR]&PSRSYNC, 0, 3, 6, 8, 10, 16, 20, oscillator(16000000))
instantiate_prescaler(PRESCALER2,  avr_IO[GTCCR]&PSRASY,  0, 3, 5, 6, 7, 8, 10,   (avr_IO[ASSR]&AS2)?oscillator(32768):avr_cycle)
#define PRESCALER0 PRESCALER01
#define PRESCALER1 PRESCALER01

/* the "temp" register to get 16-bit reads/writes */
static unsigned char TEMP;
/* offsets to derive the counters from the free-running prescaler */
static unsigned long long timer[3];
static int timer_ofs[3];
/* number of overflow events to catch up on */
static volatile unsigned timer_overflows[3];

#define fetch_timer(n) \
	PRESCALER##n(&timer[n], TCCR##n##B, TIFR##n, TIMSK##n, n==1? 16 : 8, timer_ofs[n], &timer_overflows[n])

#define set_timer(n, val) \
	fetch_timer(n); \
	timer[n] -= timer_ofs[n]; \
	timer[n] += timer_ofs[n] = (val) - (timer[n]&(n==1? 0xFFFF: 0xFF));


/* a routine for asynchronously polling the timer */
static void timer_poll_handler(int sig)
{
	fetch_timer(0);
	fetch_timer(1);
	fetch_timer(2);
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
			assert(putchar(c) != EOF);
#ifdef BAUD
			usleep(10000000/BAUD);
#endif
		}
		OR(avr_IO[UCSR0A], UDRE); // in case it is cleared due to a reset
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
		break;
	case UCSR0A:
		if(rdbr_num < sizeof rdbr_buffer) { /* in case it is cleared by a reset */
			OR(avr_IO[UCSR0A], RXC);
		}
#else
		int c;
	case UDR0:
		avr_IO[port] = getchar();
		AND(avr_IO[UCSR0A], ~RXC);
	case UCSR0A:
		if((avr_IO[UCSR0A] & RXC) == 0 && (c=getchar()) != EOF) {
			OR(avr_IO[UCSR0A], RXC|UDRE), ungetc(c, stdin);
		} else {
			OR(avr_IO[UCSR0A], UDRE);
		}
#endif
		if(avr_IO[UCSR0A] & avr_IO[UCSR0B] & (RXC|UDRE))
			avr_INT = 1;
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
		assert(putchar(c) != EOF);
		OR(avr_IO[UCSR0A], TXC|UDRE);
		if(avr_IO[UCSR0B] & (TXC|UDRE))
			avr_INT = 1;
		break;
#endif
	case UCSR0A:
		/* only allow writing the R/W parts */
		avr_IO[port] = prev&~0x43 | (avr_IO[port]&0x43 | ~prev&TXC) ^ TXC;
		break;
	case UCSR0B:
		avr_io_in(UCSR0A);
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
	case TIFR2:
		avr_IO[port] = 0; /* any write clears the flag */
		break;
	case TCNT0:
		avr_cycle++;
		set_timer(0, avr_IO[TCNT0]);
		avr_cycle--;
		break;
	case TCNT2:
		avr_cycle++;
		set_timer(2, avr_IO[TCNT2]);
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
	case TCCR2B:
		set_timer(2, avr_IO[TCNT2]);
		break;
	case TCCR1B:
		set_timer(1, avr_IO[TCNT1L]+avr_IO[TCNT1H]*0x100);
		break;
	case GTCCR:
		avr_IO[port] ^= prev&(PSRASY|PSRSYNC);
		PRESCALER01(NULL, GTCCR, 0,0,0, 0, NULL); /* resets the prescaler if demanded */
		PRESCALER2 (NULL, GTCCR, 0,0,0, 0, NULL);
		avr_IO[port] ^= prev&(PSRASY|PSRSYNC);
		PRESCALER01(NULL, GTCCR, 0,0,0, 0, NULL);
		PRESCALER2 (NULL, GTCCR, 0,0,0, 0, NULL);
		if(!(avr_IO[port]&TSM))
			avr_IO[port] = 0;
		break;
	case WDTCSR:
		if(avr_cycle-last_wdce > 4 || avr_IO[MCUSR]&WDRF) {
			avr_IO[port] = prev&0x2F | avr_IO[port]&~0x27;
		}
		if(avr_IO[port]&(WDCE|WDE))
			last_wdce = avr_cycle;
		avr_IO[port] &= ~(WDCE | avr_IO[port]&WDIF);
		break;

#define PINA  0x00
#define PORTA 0x02
#define PINB  0x03
#define PORTB 0x05
#define PINC  0x06
#define PORTC 0x08
#define PIND  0x09
#define PORTD 0x0B
	case PINA:
	case PINB:
	case PINC:
	case PIND:
		prev = avr_IO[port+2];
		avr_IO[port+2] ^= avr_IO[port];
		avr_IO[port] = 0;
		port+=2;
		int i;
	case PORTA:
	case PORTB:
	case PORTC:
	case PORTD:
		for(i=0; i<8; i++) if((avr_IO[port]&~prev)&(1<<i))
			fprintf(stderr, "<%c%u>", 'A'+(port-2)/3, i);
		break;
	}
}

void avr_des_round(unsigned long long* data, unsigned long long* key, int round, int decrypt)
{
	extern void des_init(void);
	extern unsigned long long des_round(unsigned long long block, unsigned long long *key, int round, int decrypt);
	static char initialized;
	if(!initialized) des_init(), initialized++;
	*data = des_round(*data, key, decrypt?15-round:round, decrypt);
}

static void ctrl_handler(int sig)
{
	static int count;    /* fallback */
	INT_reason = sig==SIGINT? XRESET : POWEROFF;
	avr_INT = 1;
	avr_SREG = 0x80;
	if(sig==POWEROFF && count++) abort();
}

static struct termios stdin_termios;
static const char *pty_link;

static void restore_state()
{
	if(pty_link) unlink(pty_link);
	tcsetattr(STDIN_FILENO, TCSANOW, &stdin_termios);
}

static void killed()
{
	abort();
}

static pthread_t signal_thread;
static volatile char kill_with_fire;

static void *signal_catcher(void *arg)
{
	for(;;)
		if((kill_with_fire=1), INT_reason != INTR || (kill_with_fire=0))
			avr_SREG = 0x80; /* force-quit the emulator */
		else usleep(1000000);
}

#define SPMCSR 0x37

void avr_self_program(int addr, int value)
{
	fprintf(stderr, "%02x: %04X <- %02X\n", avr_IO[SPMCSR], addr, value);
	if((avr_IO[SPMCSR]&0x3F) == 0x01)
		avr_FLASH[addr/2&0x1FFFF] = value;
	avr_IO[SPMCSR] = 0x00; //&= ~0x01;
}

int main(int argc, char **argv)
{
	tcgetattr(STDIN_FILENO, &stdin_termios);
	atexit(restore_state);

	if(argv[1] && strncmp(argv[1], "-pty", 4) == 0) {
		extern const char* make_stdin_pty(void);
		const char *pty = make_stdin_pty();
		const char *sym = argv[1]+5;
		if(sym[-1] != '\0') {
			if(symlink(pty, sym) != 0) {
				fprintf(stderr, "could not create symbolic link %s\n", sym);
				return 2;
			}
			pty_link = pty = sym;
		}
		fprintf(stderr, "connecting terminal: %.*s%s\n", (pty[0]!='/')*2, "./", pty_link=pty);
		++argv;
	}

	memset(avr_FLASH, 0xFF, 0x40000);
	if(!argv[1]) {
		fprintf(stderr, "usage: tester [-pty[:symlink]] flash.hex [eeprom.hex]]\n");
		return 2;
	} else {
		int n = ihex_read(argv[1], avr_FLASH, 0x40000, &avr_BOOT_PC);
		if(n < 0)  {
			fprintf(stderr, "could not read %s\n", argv[1]);
			return 2;
		}
		avr_BOOT_PC >>= 1;
		fprintf(stderr, "%d bytes read, startup at %04lX\n", n, avr_BOOT_PC);
	}

	memset(eeprom, 0xFF, sizeof eeprom);
	if(argv[2]) {
		int n = ihex_read(eeprom_file=argv[2], eeprom, sizeof eeprom, NULL);
		if(n < 0) {
			fprintf(stderr, "could not read %s\n", argv[2]);
			return 2;
		}
		fprintf(stderr, "%d bytes nonvolatile eeprom\n", n);
		eeprom_nonvolatile = n;
	}

	signal(SIGINT,  ctrl_handler);
	signal(SIGQUIT, ctrl_handler);
	signal(SIGABRT, restore_state);
	signal(SIGTERM, killed);

	if(isatty(STDOUT_FILENO)) {
		setbuf(stdout, NULL);
	} else {
		/* make sure output appears soon-ish in a pipe */
		struct stat s;
		if(fstat(STDOUT_FILENO, &s) == 0 && S_ISFIFO(s.st_mode))
			setvbuf(stdout, NULL, _IOLBF, 0);
	}
	if(isatty(STDIN_FILENO)) {
		struct termios ctrl = stdin_termios;
		ctrl.c_lflag &= ~ICANON; /* make stdin unbuffered */
		tcsetattr(STDIN_FILENO, TCSANOW, &ctrl);
	}

	avr_reset();
	avr_IO[MCUSR]  = PORF;
	/* avr_IO[WDTCSR] |= WDE; uncomment this to start the watchdog timer by default */
#ifdef THREAD_IO
	pthread_create(&tty_thread, NULL, fake_console, NULL);
	pthread_create(&rbr_thread, NULL, fake_receiver, NULL);
#else
	signal(SIGIO, io_input_handler);
	fcntl(STDIN_FILENO, F_SETOWN, getpid());
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_ASYNC | O_NONBLOCK);
#endif
	pthread_create(&signal_thread, NULL, signal_catcher, NULL);
#ifdef THREAD_TIMER
	{
		/* block the main CPU thread from getting bogged down with the timer */
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGALRM);
		sigaddset(&set, SIGVTALRM);
		pthread_sigmask(SIG_BLOCK, &set, NULL);
	}
	signal(SIGALRM, timer_poll_handler);
	ualarm(THREAD_TIMER, THREAD_TIMER);
#endif
	signal(SIGVTALRM, watchdog);
	uvalarm(1024*1000000ull/(WD_FREQ), 1024*1000000ull/(WD_FREQ));
	do {
		avr_IO[WDTCSR] |= avr_IO[MCUSR]&WDRF;
		switch( avr_run() ) {
		case 0:
			if(INT_reason == POWEROFF) {
				fprintf(stderr, "%s\n", "powered down");
				avr_reset();
				avr_IO[MCUSR] = BORF;
				break;
			} else if(INT_reason == XRESET) {
				fprintf(stderr, "%s\n", "external reset");
				avr_reset();
				avr_IO[MCUSR] = EXTRF;
				reset;
			} else if(INT_reason == WDRESET) {
				fprintf(stderr, "%s\n", "watchdog reset");
				avr_reset();
				avr_IO[MCUSR] = WDRF;
				reset;
			} else if(avr_IO[WDTCSR] & WDIF) {
				fprintf(stderr, "%s\n", "watchdog interrupt");
				avr_PC = vec_WDIF;
				avr_IO[WDTCSR] &=~WDIF;
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
			} else if(timer_overflows[2]) {
				avr_IO[TIFR2] &= ~TOV;
				avr_PC = vec_TOV2;
				if(--timer_overflows[2]) avr_INT = 1;
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
					avr_INT = 1; /* there might be more IO-related interrupts */
					avr_PC = vec_UDRE;
					continue;
				case TXC:  /* TX complete - clear flag, set UDRE */
					avr_INT = 1;
					avr_PC = vec_TXC;
					AND(avr_IO[UCSR0A], ~TXC);
					continue;
				default:
					if(avr_IO[UCSR0B] & avr_IO[UCSR0A] & RXC) {
						avr_INT = 1;
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
			if(!(avr_SREG & 0x80)) goto wait_for_reset;
			do {
				avr_cycle+=2;
			wait_for_interrupt:
				timer_poll_handler(SIGALRM);
				sched_yield();
			} while(!avr_INT);
			continue;
		case 2:
			fprintf(stderr, "%s\n", "breakpoint");
			do {
				avr_debug(avr_PC);
				//getchar();
			} while(avr_step() == 0);
			break;
		case 3:
			fprintf(stderr, "%s\n", "mcu spinlocked");
			if(avr_SREG & 0x80) goto wait_for_interrupt;
			wait_for_reset:
			fprintf(stderr, "%s\n", "halted");
			if(!pty_link) break;
#ifdef HALT_QUIT
			/* this keeps a named terminal alive until someone can read from it */
			fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) & ~O_NONBLOCK);
			while(getchar()!=EOF) sched_yield();
			break;
#else
			while(!(avr_INT && INT_reason != INTR)) { /* wait for a hard reset */
#    ifndef NOHUP
				struct pollfd info[1] = { STDOUT_FILENO, POLLHUP, };
				if(poll(info, 1, 0) != 0) { /* exception: stop if no-one is listening */
					/* exception: ignore the HUP of the programmer */
					static int hup_count = 0;
					if(avr_BOOT_PC && hup_count++ == 0) {
						while(!avr_INT && poll(info, 1, 0) != 0) sched_yield();
						continue;
					}
					fprintf(stderr, "%s\n", "hangup");
					goto halt;
				}
#    endif
				sched_yield();
			}
			continue;
#endif
		default:
			fprintf(stderr, "unexpected situation: PC=%04lx instruction=%04x\n", avr_PC-1, avr_FLASH[avr_PC-1]);
			break;
		}
		break;
	} while(1);
#ifdef THREAD_IO
	while(uart_num) sched_yield();
#endif
halt:	fprintf(stderr, "%s\n", "done");

	avr_debug(avr_PC-1);
	return 0;
}

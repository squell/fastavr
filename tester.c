#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "ihexread.h"

extern volatile unsigned long long avr_cycle;
extern volatile unsigned long avr_last_wdr;
extern volatile unsigned char avr_IO[];
extern volatile unsigned char avr_INT;
extern volatile unsigned long avr_INTR;

extern unsigned long avr_PC;
extern unsigned char avr_ADDR[];
extern unsigned char avr_FLASH[];
extern unsigned short int avr_SP;
extern unsigned char avr_SREG;

/* watchdog as a seperate thread */

pthread_t thread;

#define MCUSR (avr_IO[0x34])
#define WDTCR (avr_IO[0x21])

unsigned char wdtcr;

enum wdtcr_bits {
    WDIF = 1<<7, WDIE = 1<<6, WDCE = 1<<4, WDE  = 1<<3
};

void *watchdog(void *threadid)
{
    unsigned long last_wdr = 0;
    unsigned long timer = 0;
    for(;;) {
        usleep(1024);
        wdtcr = WDTCR;
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
                avr_INT = 1;
            } else if(wdtcr & WDE) {
                wdtcr = 0;
                timer = 0;
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

/* simple avr emulation */

void avr_debug(unsigned long ip)
{
    int i;
    printf("%10lld: ", avr_cycle);
    for(i=0; i < 32; i++) printf("%02x ", avr_ADDR[i+0x00]);
    printf("SP=%04x", avr_SP);
    //printf("FLAG=%02x", avr_SREG);
    //printf("SP=%04x, IP=%04lx", avr_SP, ip);
    //for(i=0; i < 3; i++) printf("%02x ", avr_ADDR[avr_SP+i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    int i;
    int n = ihex_read(argv[1], avr_FLASH, 0xFFFF);
    if(n < 0)  {
        printf("could not read %s\n", argv[1]);
        return 2;
    }
    printf("%d bytes read\n", n);
    avr_reset();
    puts("starting watchdog");
    MCUSR |= 0x1;  /* set PORF */
    WDTCR |= WDE;  /* enable the watchdog */
    pthread_create(&thread, 0, watchdog, 0);
    do {
        switch( avr_run() ) {
        case 0:
            if(wdtcr) {
                puts("watchdog interrupt");
                avr_PC = 0xC;
                WDTCR &=~WDIF;
            } else {
                puts("watchdog reset");
                avr_reset();
                MCUSR |= 0x8; /* set WDRF */
                break;
            }
            continue;
        case 1:
            puts("mcu idle");
            break;
        case 2:
            puts("breakpoint");
            break;
        default:
            printf("unexpected situation: PC=%04lx instruction=%04x\n", avr_PC, avr_FLASH[avr_PC]);
            break;
        }
        break;
    } while(1);
    puts("done");
    avr_debug(avr_PC);
    return 0;
}

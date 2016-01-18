#include <stdio.h>
#include "ihexread.h"

extern unsigned long long avr_cycle;
extern unsigned long avr_PC;
extern unsigned char avr_ADDR[];
extern unsigned char avr_FLASH[];
extern unsigned short int avr_SP;
extern unsigned char avr_SREG;
extern int avr_run(void);

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
    printf("%d bytes read\n", n);
    //for(i=0; i < n; i++) printf("%02x ", avr_FLASH[i]);
    printf("\n");
    avr_reset();
    while(! avr_run() );
    //printf("%lld\n", avr_cycle);
    avr_debug(-1);
}

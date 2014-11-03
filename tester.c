#include <stdio.h>
#include "ihexread.h"

extern unsigned long long avr_cycle;
extern unsigned long avr_IP;
extern unsigned char avr_ADDR[];
extern unsigned char avr_FLASH[];
extern unsigned short int avr_SP;
extern void avr_run(void);

void avr_io_in(void)
{
}

void avr_io_out(void)
{
}

void avr_deb(int x)
{
    printf("%04x\n", x);
}

void avr_debug(unsigned long ip)
{
    int i;
    printf("%10lld: ", avr_cycle);
    for(i=0; i < 32; i++) printf("%02x ", avr_ADDR[i+0x00]);
    printf("SP=%04x", avr_SP);
    //printf("SP=%04x, IP=%04lx", avr_SP, ip);
    //for(i=0; i < 3; i++) printf("%02x ", avr_ADDR[avr_SP+i]);
    printf("\n");
}

extern void avr_run(void);

int main(void)
{
    int i;
    int n = ihex_read("a.hex", avr_FLASH, 0xFFFF);
    printf("%d bytes read\n", n);
    //for(i=0; i < n; i++) printf("%02x ", avr_FLASH[i]);
    printf("\n");
    avr_SP = 511+0x60;
    avr_run();
    //printf("%lld\n", avr_cycle);
}

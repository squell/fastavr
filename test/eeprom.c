#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

/* to compile:
 avr-gcc -mmcu=atmega2560 eeprom.c
 avr-objcopy -j .text -j .data -O ihex a.out flash.hex
 avr-objcopy -j .eeprom --change-section-lma .eeprom=0 -O ihex a.out eeprom.hex 
 tester flash.hex eeprom.hex
 */

uint8_t EEMEM counter;
uint8_t volatile x;

ISR(EE_READY_vect)
{
    eeprom_update_byte(&counter, x+1);
    EECR |= ~EERIE;
	sleep_cpu();
}

int main()
{
    x = eeprom_read_byte(&counter);
#ifdef INTR
    EECR |= _BV(EERIE);
    sei();
    while(EECR&_BV(EERIE));
#else
    eeprom_update_byte(&counter, x+1);
#endif
    cli();
    sleep_cpu();
}

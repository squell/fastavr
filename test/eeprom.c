#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

/* to compile:
 avr-gcc -mmcu=atmega256 eeprom.c
 avr-objcopy -j .text -j .data -O ihex a.out flash.hex
 avr-objcopy -j .eeprom --change-section-lma .eeprom=0 -O ihex a.out eeprom.hex 
 tester flash.hex eeprom.hex
 */

uint8_t EEMEM counter;

int main()
{
    uint8_t x = eeprom_read_byte(&counter);
    eeprom_update_byte(&counter, x+1);
    cli();
    sleep_cpu();
}

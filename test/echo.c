#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#define F_CPU   16000000

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

int main (void)
{
   char ReceivedByte;

   UCSR0B = (1 << RXEN0) | (1 << TXEN0);
   UCSR0C = (1 << UCSZ00) | (1 << UCSZ01);

   UBRR0H = (BAUD_PRESCALE >> 8);
   UBRR0L = BAUD_PRESCALE;

   for (;;)
   {
      while ((UCSR0A & (1 << RXC0)) == 0) {};
      ReceivedByte = UDR0;

      while ((UCSR0A & (1 << UDRE0)) == 0) {};
      UDR0 = ReceivedByte;
   }
}

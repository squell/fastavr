#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#define F_CPU   16000000

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

int main (void)
{
   UCSR0B = (1 << RXEN0) | (1 << TXEN0);
   UCSR0C = (1 << UCSZ00) | (1 << UCSZ01);

   UBRR0H = (BAUD_PRESCALE >> 8);
   UBRR0L = BAUD_PRESCALE;

   UCSR0B |= (1 << RXCIE0);
   sei();

   for (;;)
   {

   }
}

volatile unsigned char p, q, buffer[256];

ISR(USART0_UDRE_vect)
{
   if(p == q)
     UCSR0B &= ~(1 << UDRIE0);
   else
     UDR0 = buffer[q++];
}

ISR(USART0_RX_vect)
{
#ifdef FLOW_LIMIT
   if((p+1 & 0xFF) == q) {
       while(!(UCSR0A & 1<<UDRE0)) ;
       UDR0 = buffer[q++];
   }
#endif
   buffer[p++] = UDR0;
   UCSR0B |= (1 << UDRIE0);
}

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

volatile unsigned char ticks;

ISR(TIMER1_OVF_vect)
{
  ticks++;
}

int main(void)
{
	sei();
	TCCR1B = (1 << CS10);
	TIMSK1 |= (1 << TOIE1);
	while(!ticks) ;
	cli();
	sleep_cpu();
	return 0;
}




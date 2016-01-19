; test the watchdog functionality; this sets the MCU to sleep as soon as
; the watchdog interrupt is fired

begin:
ldi r16, 0x69 ; set prescale to max, WDE and WDIE
out 0x21, r16
sei
.rept 9
rjmp begin
.endr
sleep

; test the watchdog functionality; this sets the MCU to sleep as soon as
; the watchdog interrupt is fired

ldi r17, 0x30 ; set wdce
ldi r16, 0x69 ; set prescale to max, WDE and WDIE
out 0x21, r17
nop
nop
nop
out 0x21, r16
sei
begin: .rept 0x18-9  ; change to something else to get a watchdog reset
rjmp begin
.endr
bla: rjmp bla
sleep
.rept 100
rjmp bla
.endr

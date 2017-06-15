; preventing the watchdog from being cleared

ldi r16, 0x30
out 0x21, r16
ldi r16, 0x69 ; set prescale to max, WDE and WDIE
out 0x21, r16
ldi r16, 0x61
sei
begin:
.rept 0x18-7
rjmp begin
.endr
bla: rjmp bla
out 0x21, r16 ; try to set watchdog to interrupt only
.rept 100
rjmp bla
.endr


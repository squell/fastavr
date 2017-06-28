; preventing the watchdog from being cleared

ldi r16, 0x30
sts 0x60, r16
ldi r16, 0x69 ; set prescale to max, WDE and WDIE
sts 0x60, r16
ldi r16, 0x61
sei
begin:
.rept 0x18-7
rjmp begin
.endr
bla: rjmp bla
sts 0x60, r16 ; try to set watchdog to interrupt only
.rept 100
rjmp bla
.endr


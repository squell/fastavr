; synthetic benchmark for the emulator; mostly executes a lot of nops

.text

loop:
.rept 100
nop
.endr
.irp reg, 0, 1, 2
dec \reg
breq 1f
rjmp loop
1:
.endr
sleep

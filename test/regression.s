# this used to trigger a brilliant bug in the emulator; this program should not crash the emulator

.text
    ldi r16, 1
    push r16
    ret

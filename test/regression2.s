# this used to trigger a segfault; note that this code doesn't well-defined

.text

   sei
   out 0x3D, r0
   out 0x3E, r0 
   jmp 1f

.org 0x200
1: ldi r31, pm_hi8(1f)
   ldi r30, pm_lo8(1f)
   icall
1: call 1f
1: rcall 1f
1: sleep

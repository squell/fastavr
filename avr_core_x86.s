/*

    AVR simulator (x86 version)
    Copyright (C) 2014, 2016 Marc Schoolderman

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

.intel_syntax noprefix
.altmacro

/* chip options -- consult your datasheet; examples:

Atmega2560: SRAM=8192, FLASHEND=0x1FFFF, IOEND=0x1FF
Atmega328:  SRAM=2048, FLASHEND= 0x3FFF, IOEND= 0xFF
Attiny85:   SRAM= 512, FLASHEND=  0xFFF, IOEND= 0x5F

*/
SRAM     = 8192
FLASHEND = 0x1FFFF
IOEND    = 0x1FF

/* functional options */
ABORTDETECT=0	# detect RJMP -1 as a halting condition?
INTR=1		# enable interrupt functionality? (turn this off to get a little bit more speed)

/* debugging switch; used for debugging the simulator itself -- produces traces by calls to avr_debug */
DEBUG=0

/* optimization options */
FASTRESUME=1	# eliminate a constant jump from the instruction decoding cycle -- keep this on!
FASTFLAG=1	# use a lookup table to convert x86 flags to AVR
FASTLDST=1	# use a different sequence of CMOVcc for deciding between loads and stores

/* branch prediction options; set to 0 if a predictable choice can be made, 1 for mixed code */
PAR_LDS=1	# use branchless code to distinguish LD/ST from LDS/STS?
PAR_STK=1	# use branchless code to distinguish PUSH/POP from LD/ST?
PAR_SBIW=1	# use branchless code to distinguish ADIW and SBIW?

/* don't touch these */
RAMEND   = SRAM + IOEND
BIGPC    = FLASHEND > 0xFFFF

/* user interface:

   recommended to declare the following data as volatile/atomic:

   byte avr_DATA[]	the data-addressable space of the AVR (including CPU registers, I/O registeres)
   byte avr_IO[]	the I/O-addressable space of the AVR (aliassed with avr_DATA)
   byte avr_INT		set this to 1 to trigger an interrupt in avr_run()

   the following are not guaranteed to be meaningful when accessed/modified when avr_run is active:

   byte avr_SREG	the avr flags registers (equal to avr_IO[0x3F])
   word avr_SP		the avr stack pointer   (equal to avr_IO[0x3D]|avr_IO[0x3E<<8)
   word avr_PC		the avr program counter

   callable functions:

   void avr_reset()	resets the avr (doesn't clear the SRAM/registers/etc)
   int avr_run()	runs the avr until sleep/break or an interrupt occurs
   int avr_step()	as avr_run(), but executes only a single instruction

	return status: 0=interrupt, 1=sleep, 2=break, 3=rjmp -1, else: unhandled

   the following optional functions, if defined by the user, will be used as follows:

   void avr_in(int port)
   void avr_out(int port)
   			called right before IN/after OUT instructions (or data access to I/O),
			with the affected port as an argument (default: do nothing);
			if port = 0x3F, avr_SREG can be accessed/modified
   void avr_in_bit(int port, int biot)
   void avr_out_bit(int port, int biot)
   			as above, but for SBI/CBI and SBIS/SBIC instructions
			(default: call avr_in and avr_out)

   void avr_des_round(byte* data, byte* key, int round, int decrypt)
   			called when a DES instruction is executed (default: abort)
*/

.global avr_reset
.global avr_run
.global avr_step
.global avr_PC
.global avr_ADDR
.global avr_IO
.global avr_FLASH
.global avr_cycle
.global avr_last_wdr
.global avr_INT
.global avr_SP
.global avr_SREG

.weak avr_io_in
.weak avr_io_in_bit
.weak avr_io_out
.weak avr_io_out_bit
.weak avr_des_round

.text

.macro debug src
pusha
pushf
lea eax, [src]
push eax
call avr_deb
pop eax
popf
popa
.endm

.if (FLASHEND+1) & FLASHEND
.error "AVR requested whose FLASH memory is not a power of two"
.endif

/* on the avr:
IF = 1<<7
TF = 1<<6
HF = 1<<5 ; AF
SF = 1<<4 ; (emulated: V xor N)
VF = 1<<3 ; OF
NF = 1<<2 ; SF
ZF = 1<<1 ; ZF
CF = 1<<0 ; CF
*/

AF = 1<<4
OF = 1<<11
SF = 1<<7
ZF = 1<<6
CF = 1<<0

.if FASTFLAG
.data
.p2align 6

# a lookuptable translating a mangled form of EFLAGS to AVR flags
flagcvt:
.irp A, 0,1
.irp OxC, 0,1
.irp S, 0,1
.irp Z, 0,1
.irp CxSA, 0,1
.byte (\A<<5) ^ (((\OxC^(\CxSA^(\S*\A)))^\S)<<4) ^ ((\OxC^(\CxSA^(\S*\A)))<<3) ^ (\S<<2) ^ (\Z<<1) ^ (\CxSA^(\S*\A))
.endr
.endr
.endr
.endr
.endr

.text
.endif

# TODO: this emulated cpu doesn't have an actual "sign" flag; this means
# that writes to it (via SES/CLS or SBI/CBI/OUT/ST) get ignored.
# converts ebx from x86 FLAGS to avr avr_SREG -> eax
.macro avr_flags ebx
    .if FASTFLAG
    and ebx, 0x8d1
    lea eax, [ebx*8+ebx]
    xor al, ah
    and eax, 0x1F
    mov al, [flagcvt+eax]
    mov ah, [avr_SREG]
    and ah, 0xC0
    or al, ah
    mov [avr_SREG], al
    .exitm
    .endif
    mov eax, ebx
    and eax, 0x8d1
    lea eax, [eax*8+eax]
    and eax, 0x4e11
    or ah, al
    and ax, 0x01FFE
    add ah, al
    mov al, ah
    add al, al
    xor al, ah
    add al, al
    and al, 0x10
    or ah, al
    mov al, [avr_SREG]
    and al, 0xC0
    or al, ah
    mov [avr_SREG], al
.endm

# converts the given flags (in edx) back to x86 FLAGS (in ebx)
.macro load_flags ebx
    mov ah, al
    mov ecx, eax
    shl cl, 2
    shr ecx, 3
    and ecx, 0xF0
    and eax, 0xF0F
    lea ebx, [ecx+eax]
.endm

.macro imm
    movzx eax, byte ptr [avr_FLASH+edi*2-1]
    shl eax, 4
    and ecx, 0xF
    or ecx, eax
    and edx, 0xF
.endm

.macro transfer edx, esi
    # don't use jumps, just read the locations
    # if CF, store, otherwise, load
.if FASTLDST
    mov eax, edx
    cmovc edx, esi
    cmovc esi, eax
    mov al, [avr_ADDR+esi]
    mov [avr_ADDR+edx], al
.else
    mov al, [avr_ADDR+esi]
    mov cl, [avr_ADDR+edx]
    cmovc eax, ecx
    cmovnc ecx, eax
    mov [avr_ADDR+esi], al
    mov [avr_ADDR+edx], cl
.endif
.endm

.macro iosignal dir, port, bitmask=0xFF
    push edx
    push ecx
    lea eax, port
    push eax
    call avr_io_\dir
    pop ecx
    pop ecx
    pop edx
.endm

.macro decode_next_instr service_ints=INTR
    and edi, FLASHEND
    movzx eax, word ptr [avr_FLASH+edi*2]

    # begin decoding the r/d, so the pipeline has something to do while jumping
    mov esi, eax
    mov edx, eax
    and esi, 0xF
    lea ecx, [esi+0x10]
    shr edx, 4
    and edx, 0x1F
    shr eax, 10
    cmovnc ecx, esi

.if DEBUG
FASTRESUME = 0
pusha
avr_flags ebx
push edi
call avr_debug
pop eax
popa
.endif
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    inc edi
.if service_ints
    mov ebp, [avr_INTR]
    jmp [decode_table+eax*4+ebp]
.else
    jmp [decode_table+eax*4]
.endif
.endm

.macro resume
.if FASTRESUME
    decode_next_instr
.else
    jmp fetch
.endif
.endm

.macro cmpc dst, reg
    mov dl, dst
    sbb dl, reg
.endm

.macro direct op, flags=, imm=, special=
    .ifc <imm>, <>
    mov al, [avr_ADDR+ecx]
    op [avr_ADDR+edx], al
    .else
	.ifc <imm>, <1>
    op byte ptr [avr_ADDR+edx], imm
	.else
    op byte ptr [avr_ADDR+edx+16], imm
	.endif
    .endif
    pushf
    .ifc <flags>, <>
    pop ebx
    .ifc <special>, <borrow>
    and ebx, ebp
    .endif
    .else
    pop eax
    and ebx, ~(flags)
    .ifnc <special>, <shift>
    and eax, flags
    or ebx, eax
    .else
    and eax, SF+ZF+CF
    or ebx, eax
    shl eax, 4
    shr al, 1
    xor ah, al
    xor al, al
    or ebx, eax
    .endif
    .endif
    resume
.endm

.macro direct1 op, flags=
    op byte ptr [avr_ADDR+edx]
    pushf
    .ifc <flags>, <>
    pop ebx
    .else
    pop eax
    and ebx, ~(flags)
    and eax,  flags
    or ebx, eax
    .endif
    resume
.endm

.p2align 3
avr_reset:
    xor eax, eax
    mov [avr_PC], eax
    mov [avr_cycle], eax
    mov [avr_cycle+4], eax
    mov [avr_last_wdr], eax
    mov word ptr [avr_SP], RAMEND
    ret

.p2align 3
avr_run:
    push ebp
    push ebx
    push edi
    push esi

    mov al, [avr_SREG]
    load_flags ebx

    mov edi, [avr_PC]
    jmp fetch

.p2align 3
fetch:
    decode_next_instr

.p2align 3
nop_movw_mul:
    test cl, 0x10
    jnz smult
    and edx, 0xF
    mov cx, [avr_ADDR+ecx*2]
    mov [avr_ADDR+edx*2], cx
    resume

.p2align 3
e_mov:
    mov al, [avr_ADDR+ecx]
    mov [avr_ADDR+edx], al
    resume

.p2align 3
e_adc:
    shr ebx, 1
    direct adc
.p2align 3
e_add:
    direct add
.p2align 3
e_sub:
    direct sub
.p2align 3
e_sbc:
    mov ebp, ebx
    or ebp, ~ZF   # the avr handles ZF oddly during the borrow operations
    shr ebx, 1
    direct sbb, ,, borrow
.p2align 3
e_cp:
    direct cmp
.p2align 3
e_cpc:
    mov ebp, ebx
    or ebp, ~ZF
    shr ebx, 1
    direct cmpc, ,, borrow
.p2align 3
e_and:
    direct and, SF+OF+ZF
.p2align 3
e_or:
    direct or,  SF+OF+ZF
.p2align 3
e_eor:
    direct xor, SF+OF+ZF

.p2align 3
e_ldi:
    movzx eax, byte ptr [avr_FLASH+edi*2-1]
    and ecx, 0xF
    and edx, 0xF
    shl eax, 4
    or ecx, eax
    mov [avr_ADDR+edx+16], cl
    resume

.p2align 3
e_ori:
    imm
    direct or, SF+OF+ZF, cl

.p2align 3
e_andi:
    imm
    direct and, SF+OF+ZF, cl

.p2align 3
e_sbci:
    imm
    mov ebp, ebx
    or ebp, ~ZF
    shr ebx, 1
    direct sbb, , cl, borrow

.p2align 3
e_subi:
    imm
    direct sub, , cl

.p2align 3
e_cpi:
    imm
    direct cmp, , cl

.p2align 3
e_sbrcs:
    mov edx, [avr_ADDR+edx]
    btr ecx, 4 # CF=0 <=> skip if clear
    sbb eax, eax
    xor edx, eax
    bt edx, ecx
    jnc skipins
    resume

.p2align 3
e_cpse:
    mov al, [avr_ADDR+edx]
    cmp al, [avr_ADDR+ecx]
    je skipins
    resume
skipins:
    mov esi, edi
    mov ax, [avr_FLASH+edi*2]
    mov edx, eax
    inc edi
    # test if we are a LDS/STS instr
    and ax, 0xFC0F
    sub ax, 0x9000
    sub ax, 1
    adc edi, 0
    # test if we are long jump/call
    and dx, 0xFE0C
    sub dx, 0x940C
    sub dx, 1
    adc edi, 0
    sub esi, edi
    neg esi
    add [avr_cycle], esi
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
e_brbs:
    avr_flags ebx
    movzx edx, word ptr [avr_FLASH+(edi-1)*2]
    shl edx, 6+16
    sar edx, 9+16
    and cl, 7
    bt eax, ecx
    lea eax, [edi+edx]
    cmovc edi, eax
    setc cl
    add [avr_cycle], ecx
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
e_brbc:
    avr_flags ebx
    movzx edx, word ptr [avr_FLASH+(edi-1)*2]
    shl edx, 6+16
    sar edx, 9+16
    and cl, 7
    bt eax, ecx
    lea eax, [edi+edx]
    cmovnc edi, eax
    setnc cl
    add [avr_cycle], ecx
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
rcall:
    movzx edx, word ptr [avr_SP]
    mov ecx, edi
.if BIGPC
    bswap ecx
    mov cl, [avr_ADDR+edx-3]   # keep the byte at SP
    mov [avr_ADDR+edx-3], ecx
    sub edx, 3
.else
    rol cx, 8
    mov [avr_ADDR+edx-1], cx
    sub edx, 2
.endif
    mov [avr_SP], dx

.p2align 3
rjmp:
    movzx edx, word ptr [avr_FLASH+(edi-1)*2]
    shl edx, 4+16
    sar edx, 4+16
.if ABORTDETECT
    cmp edx, -1
    mov esi, 3
    je exit
.endif
    lea edi, [edi+edx]
    shr eax, 3
.if BIGPC
    setc al
    lea eax, [eax*2+1]
    add dword ptr [avr_cycle], eax
.else
    adc dword ptr [avr_cycle], 1
.endif
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
e_bst_bld:
    test cl, 0x10
    jnz e_bst
e_bld:
    mov al, [avr_SREG]
    and cl, 7
    shr al, 6
    and al, 1
    xor ax, 0x0101
    sal eax, cl
    mov cl, [avr_ADDR+edx]
    or cl, ah
    xor cl, al
    mov [avr_ADDR+edx], cl
    resume
.p2align 3
e_bst:
    mov al, [avr_ADDR+edx]
    and cl, 7
    shr al, cl
    and al, 1
    sal al, 6
    mov cl, [avr_SREG]
    and cl, 0xBF
    or cl, al
    mov [avr_SREG], cl
    resume

.p2align 3
io_in1:
    avr_flags ebx      # might read sreg
    iosignal in, [ecx+0x20]
    mov al, [avr_IO+ecx+0x20]
    mov [avr_ADDR+edx], al
    resume
.p2align 3
io_in:
    iosignal in, [ecx]
    mov al, [avr_IO+ecx]
    mov [avr_ADDR+edx], al
    resume

.p2align 3
io_out1:
    avr_flags ebx      # might modify sreg
    mov al, [avr_ADDR+edx]
    mov [avr_IO+ecx+0x20], al
    iosignal out, [ecx+0x20]
    mov al, [avr_SREG]
    load_flags ebx
    resume
.p2align 3
io_out:
    mov al, [avr_ADDR+edx]
    mov [avr_IO+ecx], al
    iosignal out, [ecx]
    resume

# 1001 1000 AAAA Abbb CBI
# 1001 1010 AAAA Abbb SBI
# 1001 1001 AAAA Abbb SBIC
# 1001 1011 AAAA Abbb SBIS
.p2align 3
io_bit:
    btr ecx, 3
    rcl edx, 1
    btr edx, 5 # CF <-> skip-ins
    jc io_bit_skip
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    btr ecx, 4 # CF = set
    jc 1f
    lock btr [avr_IO+edx], ecx
    push ecx
    push edx
    call avr_io_out_bit
    add esp, 8
    resume
1:  lock bts [avr_IO+edx], ecx
    push ecx
    push edx
    call avr_io_out_bit
    add esp, 8
    resume

io_bit_skip:
    btr ecx, 4 # CF = skip if set
    setc al
    push eax
    push ecx
    push edx
    call avr_io_in_bit
    pop edx
    pop ecx
    pop eax
    bt [avr_IO+edx], ecx
    sbb al, 0  # ZF = condition matched
    jz skipins
    resume

/*

1001 00sd dddd XXXX

0000 LDS/STS + next word
0001 z+
0010 -z

0100 lpm Z / xch Z
0101 lpm Z+ / las Z

0110 elpm Z / lac Z
0111 elpm Z+ / lat Z

1001 y+
1010 -y

1100 x
1101 x+
1110 -x

1111 pop / push

*/

# this code is optimized for the 'happy path' (ld/st)
# with tweaks added to support lpm, lds and pop/push
.p2align 3
ld_st:
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    mov eax, ecx
    xor eax, 0xC
    and eax, 0xF
    shr eax, 3
    jnbe e_lpm
    adc eax, 0
    # eax -> 0/1/2 = use X/Y/Z
    lea eax, [X+eax*2]

    # switch eax with immediate if LDS
    mov esi, 0xF
    and esi, ecx
    .if PAR_LDS
    lea esi, [avr_FLASH+edi*2]
    cmovz eax, esi
    lea esi, [edi+1]
    cmovz edi, esi
    .else
    jnz 1f
    lea eax, [avr_FLASH+edi*2]
    inc edi
1:
    .endif

    # test for push/pop - use SP instead of X/Y/Z
    # this is a hack but saves jumps
    # if push/pop: xx01 -> pre-incremented, xx10 -> post-decremented
    inc word ptr [avr_SP]
    lea esi, [ecx+1]
    .if PAR_STK
    mov ebp, esi
    shr ebp, 4       # ebp: 10b if push, 01b if pop
    lea ebp, [ecx+0x11+ebp]

    test esi, 0xF    # switch esi/ecx if push/pop
    lea esi, [avr_SP]
    cmovz eax, esi
    cmovz ecx, ebp
    .else
    test esi, 0xF
    jnz 1f
    mov ebp, esi
    shr ebp, 4       # ebp: 10b if push, 01b if pop
    lea ecx, [ecx+0x11+ebp]
    lea eax, [avr_SP]
1:
    .endif

    movzx ebp, word ptr [eax]
    bt ecx, 1   # handle pre-decrement/post-increment here
    sbb ebp, 0
    mov esi, ebp
    bt ecx, 0
    adc ebp, 0
.if RAMEND < 256
    mov cx, bp
    mov [eax], cl
    and si, 0xFF
.else
    mov [eax], bp
.endif

    dec word ptr [avr_SP]  # final stacktweak

    # check if we may be accessing the I/O space
    cmp esi, IOEND
    jbe check_io

    # if CF, store, otherwise, load
1:  bt ecx, 4
    transfer edx, esi
    resume

check_io:
    cmp esi, 0x20
    jb 1b
    bt ecx, 4
    lea ecx, [esi-0x40]
    jc io_out1
    jmp io_in1

# TODO: only (E)LPM and (E)LPM+  implemented yet
e_lpm:
    test ecx, 0x10
    jnz e_xch_la
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    movzx esi, word ptr [Z]
.if BIGPC
    mov al, [RAMPZ]
    shl eax, 16
    or eax, esi
    test ecx, 2
    cmovnz esi, eax
.endif
    #and esi, (FLASHEND<<1)+1   # unsure if (E)LPM should exhibit wrap-around behaviour
    mov al, [avr_FLASH+esi]
    bt ecx, 0
    adc esi, 0
    mov [Z], si
.if BIGPC
    test ecx, 2
    mov ecx, esi
    cmovz ecx, eax
    shr ecx, 16
    mov [RAMPZ], cl
.endif
    mov [avr_ADDR+edx], al
    resume

# these instructions are probably geared towards a multicore AVR,
# but it won't hurt to have them.
e_xch_la:
    movzx esi, word ptr [Z]
    mov ah, [avr_ADDR+esi]
    mov al, [avr_ADDR+edx]
    mov [avr_ADDR+edx], ah
    and ecx, 3
    mov edx, eax
    mov dl, dh
    not al
    and dl, al
    not al
    mov ebp, edx
    or dl, al
    xor dh, al
    # dl=las, dh=lat, bp=lac
    # Z&NC -> xch, Z&C->las, NZ&NC->lac, NZ&C->lat
    shr ecx, 1
    cmovc eax, edx
    mov dl, dh
    cmovnc edx, ebp
    cmovnz eax, edx
    mov [avr_ADDR+esi], al
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    resume

/*
sd dddd 0000 LDS
sd dddd 0001 ld Z+
sd dddd 0010 ld -Z
????????????????????

sd dddd 0100 lpm Z / xch Z
sd dddd 0101 lpm Z+ / las Z
sd dddd 0110 elpm Z / lac Z
sd dddd 0111 elpm Z+ / lat Z

??????????????????
sd dddd 1001 ld Y+
sd dddd 1010 ld -Y
??????????????????

sd dddd 1100 ld X
sd dddd 1101 ld X+
sd dddd 1110 ld -X
sd dddd 1111 pop/push

*/

#------------------
.p2align 3
ldd_std:
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    mov esi, eax
    and eax, 0xF
    and esi, 0x3
    add esi, eax
    btr ecx, 3
    setnc al
    mov ax, [Y+eax*2]
    btr ecx, 4
    lea eax, [ecx+eax]
    lea esi, [esi*4+eax]
    # eax = use Z?
    # edx = reg
    # esi = index

    # check if we may be accessing io (without altering cf)
    lea ecx, [esi-IOEND]
    dec ecx
    js check_io_2

1:  transfer edx, esi
    resume

check_io_2:
    lea ecx, [esi-0x1F]
    dec ecx
    js 1b
    lea ecx, [ecx-0x20]
    jc io_out1
    jmp io_in1

.p2align 3
umult:
    mov al, [avr_ADDR+edx]
    mul byte ptr [avr_ADDR+ecx]
1:  test ax, ax
    sets cl
2:  mov [avr_ADDR], ax
    setz al
    shl al, 6
    and bl, ~(ZF+CF)
    or bl, cl
    or bl, al
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
smult:
    test dl, 0x10
    jnz exotic_mult
    and ecx, 0xF
    mov al, [avr_ADDR+edx+16]
    imul byte ptr [avr_ADDR+ecx+16]
    jmp 1b

/* 0ddd 0rrr - MULSU
   0ddd 1rrr - FMUL
   1ddd 0rrr - FMULS
   1ddd 1rrr - FMULSU */
exotic_mult:
    mov al, dl
    xor al, cl
    shl al, 5   # CF set -> FMUL(S), otherwise (F)MULSU
    jc fmul
    and cl, 0x7
    and dl, 0xF
    btr edx, 3  # CF set -> FMULSU, otherwise MULSU
    mov al, [avr_ADDR+edx+16]
    mov dl, [avr_ADDR+ecx+16]
    setc cl
    cbw
    imul dx
    test ax, ax   # probe ax for ZF
    mov dx, ax
    rol dx, 1     # probe ax for CF (for MULSU)
    shl ax, cl    # otherwise use the result of shl (won't affect flags if cl=0)
    setc cl
    jmp 2b

# SF=c | ~       (mulsu | fmulsu)
# CF=0 | c

fmul:
    test cl, 0x8
    jz fmuls
    and cl, 0x7
    and dl, 0x7
    mov al, [avr_ADDR+ecx+16]
    mul byte ptr [avr_ADDR+edx+16]
    shl ax, 1
    setc cl
    jmp 2b

fmuls:
    and cl, 0x7
    and dl, 0x7
    mov al, [avr_ADDR+ecx+16]
    imul byte ptr [avr_ADDR+edx+16]
    shl ax, 1
    setc cl
    jmp 2b

/*
0ddddd0??? -> 1 operand
00Bbbb1000 -> SEx/CLx
01????1000 -> MISC
0c000?1001 -> indirect jumps
0ddddd1010 -> decrement rd
00kkkk1011 -> DES
0kkkkk11ck -> abs jumps
1sKKddKKKK -> adiw/sbiw
*/
.p2align 3
e_1op_misc:
    mov eax, ecx
    and eax, 0xF
    btr ecx, 4
    jc e_sbiw_adiw
    jmp [subdecode_table+eax*4]

# this is a bit painful to write without using any further conditional jumps
e_sbiw_adiw:
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    mov eax, edx
    and eax, 0xC
    and edx, 0x13
    lea ecx, [eax*4+ecx]
    btr edx, 4           # CF set -> sbiw instruction
    mov si, [avr_ADDR+edx*2+24]
.if PAR_SBIW
    setc al
    sub si, cx
    pushf
    add cx, [avr_ADDR+edx*2+24]
    pushf
    test eax, eax
    mov eax, [esp+eax*4] # load the appropriate flags in eax
    cmovnz ecx, esi      # and the appropriate result in ecx
    mov [avr_ADDR+edx*2+24], cx
    add esp, 8
    and ebx, ~(SF+OF+ZF+CF)
    and eax, SF+OF+ZF+CF
    or ebx, eax
    resume
.else
    jc 1f
    add si, cx
    mov [avr_ADDR+edx*2+24], si
    pushf
    pop eax
    and ebx, ~(SF+OF+ZF+CF)
    and eax, SF+OF+ZF+CF
    or ebx, eax
    resume
1:  sub si, cx
    mov [avr_ADDR+edx*2+24], si
    pushf
    pop eax
    and ebx, ~(SF+OF+ZF+CF)
    and eax, SF+OF+ZF+CF
    or ebx, eax
    resume
.endif

.p2align 3
f_flag_misc:
    avr_flags ebx
    btr edx, 4
    jc f_ret
f_set_clr:
    mov ecx, edx
    xor edx, edx
    btr ecx, 3 # if CF, clear, otherwise set flag
    adc edx, 0x100
    shl edx, cl
    not dl
    or al, dh  # al contains avr_SREG
    and al, dl
    mov [avr_SREG], al
    load_flags ebx
    resume

.p2align 3
f_ret:
# 0000  -> ret
# 0001  -> reti
# 1000  -> sleep
# 1001  -> break
# 1010  -> wdr
# 11s0  -> lpm/spm implied r0 freak instruction
# 11s1  -> elpm/espm implied r0 freak instruction
    btr edx, 3
    jc f_misc

    shl dl, 7
    or [avr_SREG], dl    # set the IF in SREG if RETI
    movzx eax, word ptr [avr_SP]
.if BIGPC
    mov edi, [avr_ADDR+eax] # the junk read in the LSB is ignored later
    bswap edi
    add eax, 3
.else
    mov di, [avr_ADDR+eax+1]
    rol di, 8
    add eax, 2
.endif
    mov [avr_SP], ax

    add dword ptr [avr_cycle], 3-BIGPC
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
f_misc:
# treat sleep/break and wdr as exit conditions; let the caller decide what to do
# 000  -> sleep
# 001  -> break
# 010  -> wdr
    test edx, 0x4
    jnz f_lpm_spm_r0
    mov esi, edx
    inc esi
    jnp exit

    # watchdog reset
    mov eax, [avr_cycle]
    mov [avr_last_wdr], eax
    resume

.p2align 3
# TODO: only LPM implemented; add SPM
f_lpm_spm_r0:
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    lea ecx, [edx*2-4] # 100->100, 101->110, so (e)lpm->(e)lpm r0
    xor edx, edx
    jmp e_lpm

.p2align 3
f_com:
    .macro compl byte, ptr, dst
    xor byte ptr dst, 0xFF
    stc
    .endm
    direct1 compl, OF+SF+ZF+CF

.p2align 3
f_neg: direct1 neg

.p2align 3
f_swap:
    ror byte ptr [avr_ADDR+edx], 4
    resume

.p2align 3
f_asr:
    direct sar, OF+SF+ZF+CF, 1, shift

.p2align 3
f_lsr:
    direct shr, OF+SF+ZF+CF, 1, shift

.p2align 3
f_ror:
    .macro rcr_flags byte ptr dst, imm
    mov al, byte ptr dst
    rcr al, imm
    mov byte ptr dst, al
    inc al
    dec al # load ZF, SF
    .endm
    bt ebx, 0
    direct rcr_flags, OF+SF+ZF+CF, 1, shift

.p2align 3
f_inc:
    direct1 inc, OF+SF+ZF

.p2align 3
f_dec:
    direct1 dec, OF+SF+ZF

# 0c 000e eicall
.p2align 3
f_ind_jump:
    movzx eax, word ptr [avr_SP]
.if BIGPC
    bswap edi
    bt edx, 4    # if icall, modify stack
    mov ecx, [avr_ADDR+eax-3]
    cmovc ecx, edi
    mov cl, [avr_ADDR+eax-3]
    mov [avr_ADDR+eax-3], ecx

    xor edi, edi
    mov cl, [EIND]
    shl ecx, 16
    test edx, 1
    cmovnz edi, ecx
    lea edx, [edx*2+edx] # make sure 3 bytes are subtracted in EICALL
    shr edx, 4
.else
    rol di, 8
    bt edx, 4    # if icall, modify stack
    mov si, [avr_ADDR+eax-1]
    cmovc esi, edi
    mov [avr_ADDR+eax-1], si
    shr edx, 3
.endif
    sub eax, edx
    mov [avr_SP], ax

.if BIGPC
    mov di, word ptr [Z]
    or edx, 1
    add dword ptr [avr_cycle], edx
.else
    movzx edi, word ptr [Z]
    shr edx, 2
    adc dword ptr [avr_cycle], 1
.endif
    adc dword ptr [avr_cycle+4], 0
    resume


/* XFR: 1001 010k kkkk 11ck */
.p2align 3
f_abs_jump:
    movzx eax, word ptr [avr_SP]
    shr ecx, 1
    rcl edx, 1
    shl edx, 16
    mov dx, [avr_FLASH+edi*2]
    inc edi
.if BIGPC
    bswap edi
.else
    rol di, 8
.endif

    shr ecx, 1
.if BIGPC
    mov ecx, [avr_ADDR+eax-3]
    cmovc ecx, edi
    mov cl, [avr_ADDR+eax-3]
    mov [avr_ADDR+eax-3], ecx
.else
    mov si, [avr_ADDR+eax-1]
    cmovc esi, edi
    mov [avr_ADDR+eax-1], si
.endif
    lea esi, [eax-2+BIGPC]
    cmovc eax, esi
    mov [avr_SP], ax

    mov edi, edx
.if BIGPC
    mov al, -1
    adc al, 0
    adc al, 3
    movzx eax, al
    add dword ptr [avr_cycle], eax
.else
    adc dword ptr [avr_cycle], 2
.endif
    adc dword ptr [avr_cycle+4], 0
    resume

.if INTR
.p2align 3
interrupt:
    xor esi, esi
    cmp [avr_PC], esi               # were we in single-step mode?
    jl redo_exit
    btr dword ptr [avr_SREG], 7     # if IF is clear, ignore the interrupt
    jc 1f
    jmp [decode_table+eax*4]
1:  add dword ptr [avr_cycle], 3-BIGPC
    adc dword ptr [avr_cycle+4], 0
    dec edi
    mov [avr_INTR], esi
    movzx edx, word ptr [avr_SP]
    mov ecx, edi
.if BIGPC
    bswap ecx
    mov cl, [avr_ADDR+edx-3]
    mov [avr_ADDR+edx-3], ecx
    sub edx, 3
.else
    rol cx, 8
    mov [avr_ADDR+edx-1], cx
    sub edx, 2
.endif
    mov [avr_SP], dx
    jmp exit
.endif

.p2align 3
f_des:
    bt ebx, 4 # copy H to carry
    sbb eax, eax
    push eax
    push edx
    mov eax, offset avr_ADDR+8
    push eax
    sub eax, 8
    push eax
    call avr_des_round
    add esp, 16
    resume

unhandled:
    xor esi, esi
    dec esi
    mov si, [avr_FLASH+(edi-1)*2] # store illegal opcode in lower word

.p2align 3
redo_exit:
    sub dword ptr [avr_cycle], 1
    sbb dword ptr [avr_cycle+4], 0
    dec edi
    mov [avr_INTR], esi

# return status: 0 = interrupted, 1 = sleep, 2 = break, 3 = rjmp -1, else: unhandled
exit:
    # wrap-up
    avr_flags ebx
    mov [avr_PC], edi
    mov eax, esi
    pop esi
    pop edi
    pop ebx
    pop ebp
    ret

.if INTR
.p2align 3
avr_step:
    push ebp
    push ebx
    push edi
    push esi

    mov al, [avr_SREG]
    load_flags ebx

    mov edi, [avr_PC]
    mov dword ptr [avr_PC], -1
    mov byte ptr [avr_INT], 1
    decode_next_instr 0
.endif

.p2align 3
avr_io_in:
    ret
.p2align 3
avr_io_out:
    ret
.p2align 3
avr_io_in_bit:
    jmp avr_io_in
.p2align 3
avr_io_out_bit:
    jmp avr_io_out
.p2align 3
avr_des_round:
    jmp abort

.data

.p2align 2
decode_table:
/* 0000 00 */ .long nop_movw_mul
/* 0000 01 */ .long e_cpc
/* 0000 10 */ .long e_sbc
/* 0000 11 */ .long e_add
/* 0001 00 */ .long e_cpse
/* 0001 01 */ .long e_cp
/* 0001 10 */ .long e_sub
/* 0001 11 */ .long e_adc
/* 0010 00 */ .long e_and
/* 0010 01 */ .long e_eor
/* 0010 10 */ .long e_or
/* 0010 11 */ .long e_mov
/* 0011 00 */ .long e_cpi
/* 0011 01 */ .long e_cpi
/* 0011 10 */ .long e_cpi
/* 0011 11 */ .long e_cpi
/* 0100 00 */ .long e_sbci
/* 0100 01 */ .long e_sbci
/* 0100 10 */ .long e_sbci
/* 0100 11 */ .long e_sbci
/* 0101 00 */ .long e_subi
/* 0101 01 */ .long e_subi
/* 0101 10 */ .long e_subi
/* 0101 11 */ .long e_subi
/* 0110 00 */ .long e_ori
/* 0110 01 */ .long e_ori
/* 0110 10 */ .long e_ori
/* 0110 11 */ .long e_ori
/* 0111 00 */ .long e_andi
/* 0111 01 */ .long e_andi
/* 0111 10 */ .long e_andi
/* 0111 11 */ .long e_andi
/* 1000 00 */ .long ldd_std
/* 1000 01 */ .long ldd_std
/* 1000 10 */ .long ldd_std
/* 1000 11 */ .long ldd_std
/* 1001 00 */ .long ld_st
/* 1001 01 */ .long e_1op_misc
/* 1001 10 */ .long io_bit
/* 1001 11 */ .long umult
/* 1010 00 */ .long ldd_std
/* 1010 01 */ .long ldd_std
/* 1010 10 */ .long ldd_std
/* 1010 11 */ .long ldd_std
/* 1011 00 */ .long io_in
/* 1011 01 */ .long io_in1
/* 1011 10 */ .long io_out
/* 1011 11 */ .long io_out1
/* 1100 00 */ .long rjmp
/* 1100 01 */ .long rjmp
/* 1100 10 */ .long rjmp
/* 1100 11 */ .long rjmp
/* 1101 00 */ .long rcall
/* 1101 01 */ .long rcall
/* 1101 10 */ .long rcall
/* 1101 11 */ .long rcall
/* 1110 00 */ .long e_ldi
/* 1110 01 */ .long e_ldi
/* 1110 10 */ .long e_ldi
/* 1110 11 */ .long e_ldi
/* 1111 00 */ .long e_brbs
/* 1111 01 */ .long e_brbc
/* 1111 10 */ .long e_bst_bld
/* 1111 11 */ .long e_sbrcs
.rept INTR*64
/* INT     */ .long interrupt
.endr

subdecode_table:
/* 0000 */ .long f_com
/* 0001 */ .long f_neg
/* 0010 */ .long f_swap
/* 0011 */ .long f_inc
/* 0100 */ .long unhandled # illegal instruction
/* 0101 */ .long f_asr
/* 0110 */ .long f_lsr
/* 0111 */ .long f_ror
/* 1000 */ .long f_flag_misc
/* 1001 */ .long f_ind_jump
/* 1010 */ .long f_dec
/* 1011 */ .long f_des
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump

.bss

.p2align 3
avr_cycle:
    .long 0
    .long 0
avr_last_wdr:
    .long 0
avr_INTR:
    .long 0
avr_ADDR:
    .space 0x10000
avr_FLASH:
    .space 0x2000000
avr_PC:
    .long 0

avr_INT  = avr_INTR+1
avr_IO   = avr_ADDR+0x20
avr_SREG = avr_IO+0x3F
avr_SP   = avr_IO+0x3D

EIND = avr_IO+0x3C
RAMPZ= avr_IO+0x3B
Z    = avr_ADDR+30
Y    = avr_ADDR+28
X    = avr_ADDR+26

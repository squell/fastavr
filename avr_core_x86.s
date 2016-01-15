/*

    AVR simulator (x86 version)
    Copyright (C) 2014 Marc Schoolderman

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

.global avr_run
.global avr_IP
.global avr_ADDR
.global avr_FLASH
.global avr_cycle
.global avr_SP
.global avr_SREG

.extern avr_io_in
.extern avr_io_out

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

FASTRESUME=0
FASTFLAG=1
PAR=1
PAR_LDS=PAR
PAR_STK=PAR

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

# TODO: i think i handle S incorrectly ?
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

.macro imm
    movzx eax, byte ptr [avr_FLASH+edi*2-1]
    shl eax, 4
    and ecx, 0xF
    or ecx, eax
    and edx, 0xF
.endm

.macro decode_next_instr
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

.if 0
pusha
dec edi
push edi
call avr_debug
pop eax
popa
.endif
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    inc edi
    jmp [decode_table+eax*4]
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

.macro direct op, flags=, imm=, shift=
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
    .else
    pop eax
    and ebx, ~(flags)
    .ifc <shift>, <>
    and eax,  flags
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
    and ebx, ~flags
    and eax,  flags
    or ebx, eax
    .endif
    resume
.endm

.p2align 3
avr_run:
    push ebp
    push ebx
    push edi
    push esi

    xor ebx, ebx

    mov edi, [avr_IP]
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
e_add: direct add
.p2align 3
e_sub: direct sub
.p2align 3
e_sbc:
    shr ebx, 1
    direct sbb
.p2align 3
e_cp:  direct cmp
.p2align 3
e_cpc:
    shr ebx, 1
    direct cmpc
.p2align 3
e_and: direct and, SF+OF+ZF
.p2align 3
e_or:  direct or,  SF+OF+ZF
.p2align 3
e_eor: direct xor, SF+OF+ZF

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
    shr ebx, 1
    direct sbb, , cl

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
    mov [avr_ADDR+edx], di
    sub edx, 2
    mov [avr_SP], dx
    # TODO: cycles on xmega/reduced core tinyavr/22bit IP?

.p2align 3
rjmp:
    movzx edx, word ptr [avr_FLASH+(edi-1)*2]
    shl edx, 4+16
    sar edx, 4+16
    lea edi, [edi+edx]
    shr eax, 3
    adc dword ptr [avr_cycle], 1   
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
e_bst_bld:
    #avr_flags ebx
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

.p2align 3   # TODO test
io_in1:
    add cl, 32
.p2align 3
io_in:
    push edx
    push ecx
    call avr_io_in
    pop ecx
    pop edx
    mov [avr_ADDR+edx], al
    resume

.p2align 3
io_out1:
    add cl, 32
.p2align 3
io_out:
    movzx eax, byte ptr [avr_ADDR+edx]
    push eax
    push ecx
    call avr_io_out
    add esp, 4
    resume

# this code is optimized for the 'happy path' (ld/st)
# with tweaks added to support lpm, lds and pop/push
.p2align 3
ld_st:
    avr_flags ebx
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
    mov [eax], bp

    dec word ptr [avr_SP]  # final stacktweak

    bt ecx, 4
    # if CF, store, otherwise, load
    mov al, [avr_ADDR+esi]
    mov cl, [avr_ADDR+edx]
    cmovc eax, ecx
    cmovnc ecx, eax
    mov [avr_ADDR+esi], al
    mov [avr_ADDR+edx], cl
    resume

# TODO: only LPM and LPM+  implemented yet
e_lpm:
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    movzx esi, word ptr [Z]
    mov al, [avr_FLASH+esi]
    mov [avr_ADDR+edx], al
    bt ecx, 0
    adc esi, 0
    mov [Z], si
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
    avr_flags ebx
    btr ecx, 3
    setnc al
    mov ax, [Y+eax*2]
    btr ecx, 4
    lea ecx, [ecx+eax]
    lea esi, [esi*4+ecx]
    # eax = use Z?
    # edx = reg
    # esi = index

    # don't use jumps, just read the locations
    # if CF, store, otherwise, load
    mov al, [avr_ADDR+esi]
    mov cl, [avr_ADDR+edx]
    cmovc eax, ecx
    cmovnc ecx, eax
    mov [avr_ADDR+esi], al
    mov [avr_ADDR+edx], cl
    resume

.p2align 3
umult:
    mov al, [avr_ADDR+edx]
    mul byte ptr [avr_ADDR+ecx]
1:  test ax, ax
    bt eax, 15
2:  mov [avr_ADDR], ax
    setz al
    lea eax, [eax*8] # shl al, 6 without modifying CF
    lea eax, [eax*8]
    adc al, 0
    and ebx, ~(ZF+CF)
    and eax, (ZF+CF)
    or ebx, eax
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
    test ax, ax   # probe ax for ZF and CF in case of MULSU
    bt ax, 15
    shl ax, cl    # otherwise use the result of shl
    jmp 2b

fmul:
    test cl, 0x8
    jz fmuls
    and cl, 0x7
    and dl, 0x7
    mov al, [avr_ADDR+ecx+16]
    mul byte ptr [avr_ADDR+edx+16]
    shl ax, 1
    jmp 2b

fmuls:
    and cl, 0x7
    and dl, 0x7
    mov al, [avr_ADDR+ecx+16]
    imul byte ptr [avr_ADDR+edx+16]
    shl ax, 1
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

# this is a bit painful to write without using any further aconditional jumps
e_sbiw_adiw:
    mov eax, edx
    and eax, 0xC
    and edx, 0x13
    lea ecx, [eax*4+ecx]
    btr edx, 4           # CF set -> sbiw instruction
    setc al
    mov si, [avr_ADDR+edx*2+24]
    sub si, cx
    pushf
    add cx, [avr_ADDR+edx*2+24]
    pushf
    bt eax, 0
    mov eax, [esp+eax*4] # load the appropriate flags in eax
    cmovc ecx, esi       # and the appropriate result in ecx
    mov [avr_ADDR+edx*2+24], cx
    add esp, 8
    and ebx, ~(SF+OF+ZF+CF)
    and eax, SF+OF+ZF+CF
    or ebx, eax
    add dword ptr [avr_cycle], 1
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
f_flag_misc:
    avr_flags ebx
    btr edx, 4
    jc f_ret
f_set_clr:
    xor edx, edx
    btr ecx, 3 # if CF, clear, otherwise set flag
    adc edx, 0x100
    shl edx, cl
    not dl
    or dh, al  # al contains avr_SREG
    and dl, dh
    mov [avr_SREG], dl
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
    add eax, 2
    mov di, [avr_ADDR+eax]
    mov [avr_SP], ax

    add dword ptr [avr_cycle], 3
    adc dword ptr [avr_cycle+4], 0
    resume

.p2align 3
f_misc:
# treat sleep/break and wdr as exit conditions; let the caller decide what to do
    test edx, 0x4
    jnz f_lpm_spm_r0
    mov esi, edx
    jmp exit

.p2align 3
f_lpm_spm_r0:
#TODO: spm/lpm r0 family; not sure how yet
    jmp unhandled

.p2align 3
f_com:
    .macro compl byte, ptr, dst
    xor byte ptr dst, 0xFF
    stc
    .endm
    direct1 compl, OF+SF+ZF+CF

.p2align 3
f_neg: direct1 neg, OF+SF+ZF+CF

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
    inc al
    dec al # load ZF, SF
    rcr al, imm
    mov byte ptr dst, al
    .endm
    shr ebx, 1
    direct rcr_flags, OF+SF+ZF+CF, 1, shift

.p2align 3
f_inc:
    direct1 inc, OF+SF+ZF

.p2align 3
f_dec:
    direct1 dec, OF+SF+ZF

.p2align 3
# TODO: does notsupport EICALL/EIJMP
f_ind_jump:
    movzx eax, word ptr [avr_SP]
    bt edx, 4    # if icall, modify stack
    mov si, [avr_ADDR+eax]
    cmovc esi, edi
    mov [avr_ADDR+eax], si
    shr edx, 3
    sub eax, edx
    mov [avr_SP], ax

    movzx edi, word ptr [Z]
    shr edx, 2
    adc dword ptr [avr_cycle], 1
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

    shr ecx, 1
    mov si, [avr_ADDR+eax]
    cmovc esi, edi
    mov [avr_ADDR+eax], si
    lea esi, [eax-2]
    cmovc eax, esi
    mov [avr_SP], ax

    mov edi, edx
    adc dword ptr [avr_cycle], 2
    adc dword ptr [avr_cycle+4], 0
    resume

unhandled:
    xor esi, esi
    dec esi
    mov si, [avr_FLASH+(edi-1)*2] # store illegal opcode in lower word

exit:
    # wrap-up
    avr_flags ebx
    mov [avr_IP], edi
    mov eax, esi
    pop esi
    pop edi
    pop ebx
    pop ebp
    ret

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
/* 1001 10 */ .long unhandled # I/O space bit operations: CBI/SBI/SBIC/SBIS
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
/* 1011 */ .long unhandled # DES TODO ?
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump
/* 11xx */ .long f_abs_jump

.bss

avr_cycle:
    .long 0
    .long 0
avr_IP:    .long 0
avr_ADDR:  .space 0x10000
avr_FLASH: .space 0x10000

avr_SREG = avr_ADDR+0x5F
avr_SP   = avr_ADDR+0x5D

Z    = avr_ADDR+30
Y    = avr_ADDR+28
X    = avr_ADDR+26

;IO   = avr_ADDR+0x20
;SRAM = avr_ADDR+0x60


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

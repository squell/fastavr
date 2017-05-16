#include <openssl/des.h>
/*

 A silly DES implementation.

 Copyright (c) 2016, Marc Schoolderman

 Permission to use, copy, modify, and/or distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 PERFORMANCE OF THIS SOFTWARE.

 */

#include <stdio.h>

unsigned long long revbyte(unsigned long long x, int bits)
{
	unsigned long long y = 0;
	for( ; bits--; x >>= 8)
		y = y<<8 | x&0xFF;
	return y;
}

unsigned long long revbit(unsigned long long x, int bits)
{
	unsigned long long y = 0;
	for( ; bits--; x >>= 1)
		y = y<<1 | x&1;
	return y;
}

void dump(unsigned long long x, int bits)
{
	for( ; bits--; x>>=1)
		printf("%d", x&1);
	printf("\n");
}

/* note; there are several ways to layout bits in DES

  FIPS	      -- leftmost bit in DES is in the least significant bit
  C	      -- leftmost bit in DES is the most significant bit
  "bytewise"  -- leftmost bit in DES is the most significant bit in the left-most byte (OpenSSL)

  This code has been internally designed around the FIPS style (which requires bit-reversing the S-box).
  The final byte ordering can always be selected by fiddling with the IP, IIP and PC1 tables.

 */

#define layout(table) table

static const unsigned char IP[] = {
	58, 50, 42, 34, 26, 18, 10, 2,
	60, 52, 44, 36, 28, 20, 12, 4,
	62, 54, 46, 38, 30, 22, 14, 6,
	64, 56, 48, 40, 32, 24, 16, 8,
	57, 49, 41, 33, 25, 17,  9, 1,
	59, 51, 43, 35, 27, 19, 11, 3,
	61, 53, 45, 37, 29, 21, 13, 5,
	63, 55, 47, 39, 31, 23, 15, 7,
};

static const unsigned char IIP[] = {
	40, 8, 48, 16, 56, 24, 64, 32,
	39, 7, 47, 15, 55, 23, 63, 31,
	38, 6, 46, 14, 54, 22, 62, 30,
	37, 5, 45, 13, 53, 21, 61, 29,
	36, 4, 44, 12, 52, 20, 60, 28,
	35, 3, 43, 11, 51, 19, 59, 27,
	34, 2, 42, 10, 50, 18, 58, 26,
	33, 1, 41, 9, 49, 17, 57, 25,
};

static unsigned char IPmsb[] = {
	7, 15, 23, 31, 39, 47, 55, 63,
	5, 13, 21, 29, 37, 45, 53, 61,
	3, 11, 19, 27, 35, 43, 51, 59,
	1, 9, 17, 25, 33, 41, 49, 57,
	8, 16, 24, 32, 40, 48, 56, 64,
	6, 14, 22, 30, 38, 46, 54, 62,
	4, 12, 20, 28, 36, 44, 52, 60,
	2, 10, 18, 26, 34, 42, 50, 58,
};

static unsigned char IIPmsb[] = {
	25, 57, 17, 49, 9, 41, 1, 33,
	26, 58, 18, 50, 10, 42, 2, 34,
	27, 59, 19, 51, 11, 43, 3, 35,
	28, 60, 20, 52, 12, 44, 4, 36,
	29, 61, 21, 53, 13, 45, 5, 37,
	30, 62, 22, 54, 14, 46, 6, 38,
	31, 63, 23, 55, 15, 47, 7, 39,
	32, 64, 24, 56, 16, 48, 8, 40,
};

static const unsigned char IPbyte[] = {
	63, 55, 47, 39, 31, 23, 15, 7,
	61, 53, 45, 37, 29, 21, 13, 5,
	59, 51, 43, 35, 27, 19, 11, 3,
	57, 49, 41, 33, 25, 17, 9, 1,
	64, 56, 48, 40, 32, 24, 16, 8,
	62, 54, 46, 38, 30, 22, 14, 6,
	60, 52, 44, 36, 28, 20, 12, 4,
	58, 50, 42, 34, 26, 18, 10, 2,
};

static const unsigned char IIPbyte[] = {
	32, 64, 24, 56, 16, 48, 8, 40,
	31, 63, 23, 55, 15, 47, 7, 39,
	30, 62, 22, 54, 14, 46, 6, 38,
	29, 61, 21, 53, 13, 45, 5, 37,
	28, 60, 20, 52, 12, 44, 4, 36,
	27, 59, 19, 51, 11, 43, 3, 35,
	26, 58, 18, 50, 10, 42, 2, 34,
	25, 57, 17, 49, 9, 41, 1, 33,
};

static const unsigned char P[] = {
	16, 7, 20, 21, 29, 12, 28, 17,
	1, 15, 23, 26, 5, 18, 31, 10,
	2, 8, 24, 14, 32, 27, 3, 9,
	19, 13, 30, 6, 22, 11, 4, 25,
};

/* when using the FIPS bit layout, Sbox input and output has to be bit-mirrored */

#define mask(n) ((1ull<<(n))-1)
#define flip(x) ((x)>>3&1 | (x)>>1&2 | (x)<<1&4 | (x)<<3&8)
#define boxdef(x00,x01,x02,x03,x04,x05,x06,x07,x08,x09,x0A,x0B,x0C,x0D,x0E,x0F, \
	       x10,x11,x12,x13,x14,x15,x16,x17,x18,x19,x1A,x1B,x1C,x1D,x1E,x1F, \
	       x20,x21,x22,x23,x24,x25,x26,x27,x28,x29,x2A,x2B,x2C,x2D,x2E,x2F, \
	       x30,x31,x32,x33,x34,x35,x36,x37,x38,x39,x3A,x3B,x3C,x3D,x3E,x3F) \
	flip(x00), flip(x20), flip(x08), flip(x28), flip(x04), flip(x24), flip(x0C), flip(x2C), \
	flip(x02), flip(x22), flip(x0A), flip(x2A), flip(x06), flip(x26), flip(x0E), flip(x2E), \
	flip(x01), flip(x21), flip(x09), flip(x29), flip(x05), flip(x25), flip(x0D), flip(x2D), \
	flip(x03), flip(x23), flip(x0B), flip(x2B), flip(x07), flip(x27), flip(x0F), flip(x2F), \
	flip(x10), flip(x30), flip(x18), flip(x38), flip(x14), flip(x34), flip(x1C), flip(x3C), \
	flip(x12), flip(x32), flip(x1A), flip(x3A), flip(x16), flip(x36), flip(x1E), flip(x3E), \
	flip(x11), flip(x31), flip(x19), flip(x39), flip(x15), flip(x35), flip(x1D), flip(x3D), \
	flip(x13), flip(x33), flip(x1B), flip(x3B), flip(x17), flip(x37), flip(x1F), flip(x3F)

static const unsigned char S[][64] = {
	boxdef(
	14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
	0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
	4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
	15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13
	),

	boxdef(
	15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
	3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
	0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
	13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9
	),

	boxdef(
	10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
	13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
	13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
	1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12
	),

	boxdef(
	7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
	13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
	10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
	3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14
	),

	boxdef(
	2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
	14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
	4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
	11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3
	),

	boxdef(
	12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
	10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
	9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
	4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13
	),

	boxdef(
	4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
	13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
	1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
	6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12
	),

	boxdef(
	13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
	1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
	7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
	2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11
	),
};

static const unsigned char PC1[] = {
	57, 49, 41, 33, 25, 17, 9,
	1, 58, 50, 42, 34, 26, 18,
	10, 2, 59, 51, 43, 35, 27,
	19, 11, 3, 60, 52, 44, 36,
	63, 55, 47, 39, 31, 23, 15,
	7, 62, 54, 46, 38, 30, 22,
	14, 6, 61, 53, 45, 37, 29,
	21, 13, 5, 28, 20, 12, 4,
};

static unsigned char PC1msb[] = {
	8, 16, 24, 32, 40, 48, 56,
	64, 7, 15, 23, 31, 39, 47,
	55, 63, 6, 14, 22, 30, 38,
	46, 54, 62, 5, 13, 21, 29,
	2, 10, 18, 26, 34, 42, 50,
	58, 3, 11, 19, 27, 35, 43,
	51, 59, 4, 12, 20, 28, 36,
	44, 52, 60, 37, 45, 53, 61,
};

static const unsigned char PC1byte[] = {
	64, 56, 48, 40, 32, 24, 16,
	8, 63, 55, 47, 39, 31, 23,
	15, 7, 62, 54, 46, 38, 30,
	22, 14, 6, 61, 53, 45, 37,
	58, 50, 42, 34, 26, 18, 10,
	2, 59, 51, 43, 35, 27, 19,
	11, 3, 60, 52, 44, 36, 28,
	20, 12, 4, 29, 21, 13, 5,
};

static const unsigned char PC2[] = {
	14, 17, 11, 24, 1, 5,
	3, 28, 15, 6, 21, 10,
	23, 19, 12, 4, 26, 8,
	16, 7, 27, 20, 13, 2,
	41, 52, 31, 37, 47, 55,
	30, 40, 51, 45, 33, 48,
	44, 49, 39, 56, 34, 53,
	46, 42, 50, 36, 29, 32,
};

/* size of lookup tables for 56- and 64-bit permutations in bits;
   must be a divisor of the bit-size; maximum practical value is 16 */
#define SPEEDUP64 8 /* recommended: 4 or 8, max 16 */
#define SPEEDUP56 7 /* recommend: 4 or 7/8, max 14 */

/* precompute tables */
#if SPEEDUP64 && SPEEDUP56
static unsigned long SP[8][64];
static unsigned long long PC2F[56/SPEEDUP56][1<<SPEEDUP56];
static unsigned long long PC1F[64/SPEEDUP64][1<<SPEEDUP64];
static unsigned long long IPF [64/SPEEDUP64][1<<SPEEDUP64];
static unsigned long long IIPF[64/SPEEDUP64][1<<SPEEDUP64];
#endif

/* routines */
static unsigned long long bit_select_slow(unsigned long long data, const unsigned char *perm, int bits)
{
	unsigned long long x = 0;
	while(bits--)
		x = x<<1 | data >> perm[bits]-1 & 1;
	return x;
}

static unsigned long long bit_select_fast(unsigned long long data, unsigned long long *perm, int chunks, int bits)
{
	unsigned long long x = 0;
	int i;
	bits /= chunks;
	for(i=0; i < chunks; i++, data>>=bits)
		x |= perm[i<<bits | data&mask(bits)];
	return x;
}

#if SPEEDUP64 && SPEEDUP56
#define elems(array) (sizeof array/sizeof *array)
#define bit_select(data, table, bits) bit_select_fast(data, (void*)table##F, elems(table##F), bits)
#else
#define PC2byte PC2
#define PC2msb PC2
#define Pbyte P
#define Pmsb P
#define bit_select(data, table, bits) bit_select_slow(data, layout(table), sizeof table)
#endif

/* whats FIPS calls a left shift is actually a right shift */
static unsigned long long rot(unsigned long long x, int n, int bits)
{
	return x>>n | (x&mask(n))<<bits-n;
}

static unsigned long long ks(int n, unsigned long long key)
{
	unsigned long long l, r;
	n = 2*n - n/8 ^ (n==0 | n==15);
	l = rot(key & mask(28), n, 28);
	r = rot(key >> 28, n, 28);
	return bit_select(l | r<<28, PC2, 56);
}

static unsigned long f(unsigned long long half, unsigned long long subkey)
{
	unsigned long long x = half>>31 | half<<1 | half<<33;
	unsigned long y = 0;
	int i;
	for(i=0; i<8; i++,x>>=4,subkey>>=6)
	#if SPEEDUP64 && SPEEDUP56
		y |= SP[i][(x ^ subkey) & 0x3F];
	return y;
	#else
		y |= S[i][(x ^ subkey) & 0x3F] << 4*i;
	return bit_select(y, P, 32);
	#endif
}

static unsigned long long des_round(unsigned long long block, unsigned long long key, int round, int swap)
{
	key   = bit_select(key, PC1, 64);
	block = bit_select(block, IP, 64);
	block ^= f(block>>32, ks(round,key));
	if(swap) block = rot(block,32,64);
	return bit_select(block, IIP, 64);
}

static unsigned long long des_encrypt(unsigned long long block, unsigned long long key)
{
	int i;
	for(i=0; i<16; i++)
		block = des_round(block, key, i, 15-i);
	return block;
}

static unsigned long long des_decrypt(unsigned long long block, unsigned long long key)
{
	int i;
	for(i=16; i--; )
		block = des_round(block, key, i, i);
	return block;
}

static unsigned long long des_fast_encrypt(unsigned long long block, unsigned long long key)
{
	unsigned long l, r;
	int i;
	key   = bit_select(key, PC1, 64);
	block = bit_select(block, IP, 64);
	l = block&mask(32), r = block>>32;
	for(i=0; i<8; i++) {
		l ^= f(r, ks(2*i,  key));
		r ^= f(l, ks(2*i+1,key));
	}
	block = l << 32 | r;
	block = bit_select(block, IIP, 64);
	return block;
}

static void des_sched(unsigned long long key, unsigned long long *sched)
{
	int i;
	key = bit_select(key, PC1, 64);
	for(i=0; i<16; i++)
		sched[i] = ks(i, key);
}

static unsigned long long des_fast_encrypt_sched(unsigned long long block, unsigned long long *sched)
{
	unsigned long l, r;
	int i;
	block = bit_select(block, IP, 64);
	l = block&mask(32), r = block>>32;
	for(i=0; i<8; i++) {
		l ^= f(r, sched[2*i]);
		r ^= f(l, sched[2*i+1]);
	}
	block = l << 32 | r;
	block = bit_select(block, IIP, 64);
	return block;
}

#define lut_loop(table,orig,i,j,bits) \
	for(i=0; i<elems(table); i++) for(j=0; j<elems(*table); j++) \
		table[i][j] = bit_select_slow((unsigned long long)j<<bits/elems(table)*i, orig, sizeof orig);

void des_init(void)
{
	#if SPEEDUP64 && SPEEDUP56
	int i,j;
	char speedup64_is_divisor_of_64[1-(64%SPEEDUP64<<1)];
	char speedup56_is_divisor_of_56[1-(56%SPEEDUP56<<1)];
	for(i=0; i<8; i++) for(j=0; j<64; j++)
		SP[i][j] = bit_select_slow(S[i][j] << 4*i, P, sizeof P);
	lut_loop(PC1F,layout(PC1),i,j,64);
	lut_loop(PC2F,PC2,i,j,56)
	lut_loop(IPF,layout(IP),i,j,  64);
	lut_loop(IIPF,layout(IIP),i,j,64);
	#endif
}

int main()
{
	DES_key_schedule sched;
	unsigned long long output;
	unsigned long long data = revbit(0x0123456789abcdef,64);
	unsigned long long key = revbit(0b0001001100110100010101110111100110011011101111001101111111110001,64);
	unsigned long long keys[16];
	des_init();
	printf("%16llx\n", revbit(des_encrypt(data,key),64));
	dump(des_encrypt(data,key),64);
	dump(des_fast_encrypt(data,key),64);
	des_sched(key, keys);
	dump(des_fast_encrypt_sched(data,keys),64);

	data = revbyte(0x0123456789abcdef,8);
	key = revbyte(0b0001001100110100010101110111100110011011101111001101111111110001,8);
	DES_set_key_unchecked((void*)&key, &sched);
	DES_ecb_encrypt((void*)&data, (void*)&output, &sched, DES_ENCRYPT);
	dump(revbit(revbyte(output,8),64),64);
}

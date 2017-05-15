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
	while(bits--)
		printf("%d", !!(x&(1ull<<bits)));
	printf("\n");
}

/* note; there are several ways to layout bits in DES

  FIPS	      -- leftmost bit in DES is in the least significant bit
  C	      -- leftmost bit in DES is the most significant bit
  "bytewise"  -- leftmost bit in DES is the most significant bit in the left-most byte

  This code has been designed around the C style.

 */

#define layout(table) table

static unsigned char IP[] = {
	58, 50, 42, 34, 26, 18, 10, 2,
	60, 52, 44, 36, 28, 20, 12, 4,
	62, 54, 46, 38, 30, 22, 14, 6,
	64, 56, 48, 40, 32, 24, 16, 8,
	57, 49, 41, 33, 25, 17,  9, 1,
	59, 51, 43, 35, 27, 19, 11, 3,
	61, 53, 45, 37, 29, 21, 13, 5,
	63, 55, 47, 39, 31, 23, 15, 7,
};

static unsigned char IIP[] = {
	40, 8, 48, 16, 56, 24, 64, 32,
	39, 7, 47, 15, 55, 23, 63, 31,
	38, 6, 46, 14, 54, 22, 62, 30,
	37, 5, 45, 13, 53, 21, 61, 29,
	36, 4, 44, 12, 52, 20, 60, 28,
	35, 3, 43, 11, 51, 19, 59, 27,
	34, 2, 42, 10, 50, 18, 58, 26,
	33, 1, 41, 9, 49, 17, 57, 25,
};

static unsigned char IPle[] = {
	2, 10, 18, 26, 34, 42, 50, 58,
	4, 12, 20, 28, 36, 44, 52, 60,
	6, 14, 22, 30, 38, 46, 54, 62,
	8, 16, 24, 32, 40, 48, 56, 64,
	1, 9, 17, 25, 33, 41, 49, 57,
	3, 11, 19, 27, 35, 43, 51, 59,
	5, 13, 21, 29, 37, 45, 53, 61,
	7, 15, 23, 31, 39, 47, 55, 63,
};

static unsigned char IIPle[] = {
	33, 1, 41, 9, 49, 17, 57, 25,
	34, 2, 42, 10, 50, 18, 58, 26,
	35, 3, 43, 11, 51, 19, 59, 27,
	36, 4, 44, 12, 52, 20, 60, 28,
	37, 5, 45, 13, 53, 21, 61, 29,
	38, 6, 46, 14, 54, 22, 62, 30,
	39, 7, 47, 15, 55, 23, 63, 31,
	40, 8, 48, 16, 56, 24, 64, 32,
};

static unsigned char E[] = {
	32, 1, 2, 3, 4, 5, 4, 5,
	6, 7, 8, 9, 8, 9, 10, 11,
	12, 13, 12, 13, 14, 15, 16, 17,
	16, 17, 18, 19, 20, 21, 20, 21,
	22, 23, 24, 25, 24, 25, 26, 27,
	28, 29, 28, 29, 30, 31, 32, 1,
};

static unsigned char P[] = {
	16, 7, 20, 21, 29, 12, 28, 17,
	1, 15, 23, 26, 5, 18, 31, 10,
	2, 8, 24, 14, 32, 27, 3, 9,
	19, 13, 30, 6, 22, 11, 4, 25,
};

static unsigned char S[][64] = {
	14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
	0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
	4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
	15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13,

	15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
	3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
	0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
	13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9,

	10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
	13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
	13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
	1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12,

	7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
	13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
	10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
	3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14,

	2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
	14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
	4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
	11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3,

	12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
	10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
	9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
	4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13,

	4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
	13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
	1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
	6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12,

	13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
	1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
	7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
	2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11,
};

static unsigned char PC1[] = {
	57, 49, 41, 33, 25, 17, 9,
	1, 58, 50, 42, 34, 26, 18,
	10, 2, 59, 51, 43, 35, 27,
	19, 11, 3, 60, 52, 44, 36,
	63, 55, 47, 39, 31, 23, 15,
	7, 62, 54, 46, 38, 30, 22,
	14, 6, 61, 53, 45, 37, 29,
	21, 13, 5, 28, 20, 12, 4,
};

static unsigned char PC1le[] = {
	1, 9, 17, 25, 33, 41, 49,
	57, 2, 10, 18, 26, 34, 42,
	50, 58, 3, 11, 19, 27, 35,
	43, 51, 59, 4, 12, 20, 28,
	7, 15, 23, 31, 39, 47, 55,
	63, 6, 14, 22, 30, 38, 46,
	54, 62, 5, 13, 21, 29, 37,
	45, 53, 61, 36, 44, 52, 60,
};

static unsigned char PC2[] = {
	14, 17, 11, 24, 1, 5,
	3, 28, 15, 6, 21, 10,
	23, 19, 12, 4, 26, 8,
	16, 7, 27, 20, 13, 2,
	41, 52, 31, 37, 47, 55,
	30, 40, 51, 45, 33, 48,
	44, 49, 39, 56, 34, 53,
	46, 42, 50, 36, 29, 32,
};

static unsigned long long bit_select(unsigned long long data, const unsigned char* perm, int bits, int width)
{
	unsigned long long x = 0;
	int i=0;
	while(i < bits)
		x = x<<1 | !!(data & 1ull<<width-perm[i++]);
	return x;
}

static unsigned long sbox(unsigned char value, const unsigned char* box)
{
	return box[(value&0x21)*17 & 0x30 | value/2 & 0xF];
}

static unsigned long long rot(unsigned long long x, int n, int bits)
{
	return (x&(1ull<<bits-n)-1) << n | x >> bits-n;
}

static unsigned long long ks(int n, unsigned long long key)
{
	unsigned long long l, r;
	key = bit_select(key, layout(PC1), sizeof PC1, 64);
	n = 2*n - (n/8) ^ (n==0 | n==15);
	r = rot(key & (1ul<<28)-1, n, 28);
	l = rot(key >> 28, n, 28);
	return bit_select(r | l<<28, PC2, sizeof PC2, 56);
}

static unsigned long f(unsigned long half, unsigned long long subkey)
{
	unsigned long long x = bit_select(half, E, sizeof E, 32) ^ subkey;
	unsigned long y = 0;
	int i;
	for(i=0; i<8; i++)
		y |= sbox(x>>6*i, S[7-i]) << 4*i;
	return bit_select(y, P, sizeof P, 32);
}

static unsigned long long des(unsigned long long block, unsigned long long key)
{
	int i;
	block = bit_select(block, layout(IP), sizeof IP, 64);
	for(i=0; i<16; i++) {
		block ^= f(block, ks(i,key)) << 32;
		block = rot(block,32,64);
	}
	return bit_select(rot(block,32,64), layout(IIP), sizeof IIP, 64);
}

int main()
{
	DES_key_schedule sched;
	unsigned long long output;
	unsigned long long data = 0x0123456789abcdef;
	unsigned long long key = 0b0001001100110100010101110111100110011011101111001101111111110001;
	printf("%16llx\n", des(data,key));
	dump(des(data,key),64);

	data = revbyte(0x0123456789abcdef,8);
	key = revbyte(0b0001001100110100010101110111100110011011101111001101111111110001,8);
	DES_set_key_unchecked((void*)&key, &sched);
	DES_ecb_encrypt((void*)&data, (void*)&output, &sched, DES_ENCRYPT);
	dump(revbyte(output,8),64);
}

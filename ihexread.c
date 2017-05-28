/*

 IHEX8 reader

 Copyright (c) 2014 Marc Schoolderman

 Permission to use, copy, modify, and/or distribute this software for
 any purpose with or without fee is hereby granted, provided that the
 above copyright notice and this permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#include <stdio.h>
#include "ihexread.h"

static int nibble(const char c)
{
	/* 0x39 = 9; 0x41 = A */
	static char tab[] = {
		0,1,2,3,4,5,6,7,8,9,
		-1,-1,-1,-1,-1,-1,-1,
		10,11,12,13,14,15
	};

	if(c < '0' || c > 'F')
		return -1;
	else
		return tab[c-'0'];
}

static unsigned char hex_byte_cksum;
static int hex_byte(const char *p)
{
	int value = nibble(p[0])<<4 | nibble(p[1]);
	hex_byte_cksum -= value;
	return value;
}

ssize_t ihex_read(const char *fname, void *image_ptr, size_t capacity)
{
	unsigned char *image = image_ptr;
	FILE *f;
	char buffer[1024];
	unsigned long int segment = 0, base = 0, lnr = 0;
	size_t size = 0;

	buffer[1023] = -1;
	hex_byte_cksum = 0;

	if(!(f = fopen(fname, "r"))) {
		fprintf(stderr, "%s: could not open file\n", fname);
		return -1;
	}

	while(++lnr, fgets(buffer, sizeof buffer, f)) {
		int type, len, cksum, i;
		long int addr;
		if(buffer[1023] == '\0') {
			fprintf(stderr, "%s:%ld: lines are too long\n", fname, lnr);
			goto abort;
		}
		if(buffer[0] != ':') {
			fprintf(stderr, "%s:%ld: line not starting with ':'\n", fname, lnr);
			goto abort;
		}

		len  = hex_byte(buffer+1);
		addr = hex_byte(buffer+3)<<8 | hex_byte(buffer+5);
		type = hex_byte(buffer+7);
		if(len < 0 || addr < 0 || type < 0) {
			fprintf(stderr, "%s:%ld: not hexadecimal data\n", fname, lnr);
			goto abort;
		}

		base = addr + segment;
		if(base + len >= size)
			size = base + len;
		if(size > capacity) {
			fprintf(stderr, "%s:%ld: ihex size exceeds capacity\n", fname, lnr);
			goto abort;
		}

		if(type == 0) for(i=0; i < len; i++) {
			int byte = hex_byte(buffer+9+2*i);
			if(byte < -1) {
				fprintf(stderr, "%s:%ld: not hexadecimal data\n", fname, lnr);
				goto abort;
			}
			if(type == 0)
				image[base+i] = byte;
		} else if(type == 1) {
			fclose(f);   /* ignore the checksum */
			return size;
		} else if(type == 2 && len == 2) {
			segment = (hex_byte(buffer+9)<<8 | hex_byte(buffer+11)) * 16;
		} else {
			fprintf(stderr, "%s:%ld: unsupport frame type: type %d, %d bytes\n", fname, lnr, type, len);
			goto abort;
		}

		cksum = hex_byte(buffer+9+2*len);
		if(hex_byte_cksum != 0) {
			fprintf(stderr, "%s:%ld: checksum error\n", fname, lnr);
			goto abort;
		}
	}
	fprintf(stderr, "%s:%ld: missing end-of-file record\n", fname, lnr);
abort:
	fclose(f);
	return -1;
}

/*

 IHEX8 writer

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

ssize_t ihex_write(const char *fname, void *image_ptr, size_t bytes)
{
	unsigned char *image = image_ptr;
	size_t addr = 0;
	FILE* f;

	if(!(f = fopen(fname, "w"))) {
		fprintf(stderr, "%s: could not open file\n", fname);
		return -1;
	}

	while(addr < bytes) {
		unsigned int sum, i = bytes - addr;
		if(addr >> 16 && !(addr&0xFFFF)) {
			unsigned segment = addr>>16 & 0xFFFF;
			fprintf(f, ":02000002%04X%02X\n", segment, -(segment&0xFF)-(segment>>8&0xFF)-4 & 0xFF);
		}
		if(i > 16) i = 16;
		sum = i + (addr&0xFF) + (addr>>8&0xFF);
		fprintf(f, ":%02X%04X00", i, (unsigned int)addr&0xFFFF);
		while(i--) {
			unsigned char c = image[addr++];
			sum += c;
			fprintf(f, "%02X", c);
		}
		fprintf(f, "%02X\n", (-sum)&0xFF);
	}
	fprintf(f, ":00000001FF\n");
	return fclose(f);
}

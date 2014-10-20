#include <stdio.h>
#include <string.h>
#include <stddef.h>

struct ihex_record {
    short unsigned addr;
    unsigned char cnt, type;
    unsigned char data[0];
};

static inline int nibble(const char c)
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
static inline int hex_byte(const char *p)
{
    int value = nibble(p[0])<<4 | nibble(p[1]);
    hex_byte_cksum -= value;
    return value;
}

size_t ihex_read(const char *fname, unsigned char *image, size_t capacity)
{
    FILE *f;
    char buffer[1024];
    unsigned long int base = 0, lnr = 0;

    buffer[1023] = -1;

    if(!(f = fopen(fname, "r"))) {
	fprintf(stderr, "%s: could not open file\n", fname);
	return -1;
    }

    while(++lnr, fgets(buffer, sizeof buffer, f)) {
	int type, len, cksum, i;
	long int addr;
	if(buffer[1023] == '\0') {
	    fprintf(stderr, "%s:%d: lines are too long\n", fname, lnr);
	    goto abort;
	}
	if(buffer[0] != ':') {
	    fprintf(stderr, "%s:%d: line not starting with ':'\n", fname, lnr);
	    goto abort;
	}

	len  = hex_byte(buffer+1);
	addr = hex_byte(buffer+3)<<8 | hex_byte(buffer+5);
	type = hex_byte(buffer+7);
	if(len < 0 || addr < 0 || type < 0) {
	    fprintf(stderr, "%s:%d: not hexadecimal data\n", fname, lnr);
	    goto abort;
	}

	base = addr;
	if(base + len >= capacity) {
	    printf("%ld:%ld\n", base, addr);
	    fprintf(stderr, "%s:%d: ihex size exceeds capacity\n", fname, lnr);
	    goto abort;
	}

	for(i=0; i < len; i++) {
	    int byte = hex_byte(buffer+9+2*i);
	    if(byte < -1) {
		fprintf(stderr, "%s:%d: not hexadecimal data\n", fname, lnr);
		goto abort;
	    }
	    if(type == 0)
		image[base+i] = byte;
	}

	cksum = hex_byte(buffer+9+2*len);
	if(hex_byte_cksum != 0) {
	    fprintf(stderr, "%s:%d: checksum error\n", fname, lnr);
	    goto abort;
	}

	if(type >= 1) {
	    if(type == 1) break;
	    fprintf(stderr, "%s:%d: not an i8hex file\n", fname, lnr);
	    goto abort;
	}
    }
    fclose(f);
    return 0;

abort:
    fclose(f);
    return -1;
}

unsigned char huge[0xFFFF];

int main(void)
{
    ihex_read("test.ihex", huge, 0xFFFF);
}

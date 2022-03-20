#include <string.h>
#include <stdio.h>
#include "crc16heater.h"


// Reverse engineer custom CRC-16 profile with reveng and crcany 
// ./reveng -w 16 -s fa1b1101ff0028140a2a2c2e2800008ce4  fa1b1209c27f8e14000000000100101b71  fa1b13002d00000000700000000000bd48  fa1b13002d00000000780000000000b00a
// width=16  poly=0x1021  init=0xfa00  refin=false  refout=false  xorout=0x0000  check=0xca32  residue=0x0000  name=(none)
// ./reveng -w 16 -p 0x1021 -i 0xfa00 -x 0 fb1b00aaffffff0000 -c

// git clone https://github.com/madler/crcany.git
// make 
// echo width=16  poly=0x1021  init=0xfa00  refin=false  refout=false  xorout=0x0000  check=0xca32  residue=0x0000  name=heater | ./crcadd 
// cd src
// gcc crc16heater.c crc16heater_test.c 
// ./a.out fa1b1101ff0028140a2a2c2e280000
// expect 8ce4

int hex2bin(const char *in, char *out) { 
        int l = strlen(in);
        for (const char *p = in; p < in + l; p += 2) { 
                char b[3];
                b[0] = p[0];
                b[1] = p[1];
                b[2] = 0;
                int c;
                sscanf(b, "%x", &c);
                *(out++) = c;
        }
        return strlen(in) / 2;
}

int main(int argc, char **argv) { 
	char buf[1024];
	int l = hex2bin(argv[1], buf);
	uint16_t crc = 0xfa00;
	crc = crc16heater_word(crc, buf, l);
	printf("%04x\n", (int)crc);
	return 0;
}

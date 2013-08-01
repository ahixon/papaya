

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#include "uboot/common.h"
void NetReceive(void){
    UNIMPLEMENTED();
}

void udelay(uint32_t us){
    volatile int i;
    for(; us > 0; us--){
        for(i = 0; i < 100; i++){
        }
    }
}

unsigned long simple_strtoul(const char *cp, char **endp,
				unsigned int base)
{
	unsigned long result = 0;
	unsigned long value;

	if (*cp == '0') {
		cp++;
		if ((*cp == 'x') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}

		if (!base)
			base = 8;
	}

	if (!base)
		base = 10;

	while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
	    ? toupper(*cp) : *cp)-'A'+10) < base) {
		result = result*base + value;
		cp++;
	}

	if (endp)
		*endp = (char *)cp;

	return result;
}


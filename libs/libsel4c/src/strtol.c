/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
  Created: Fri Oct  8 2004 
*/

#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

/**
 * Work out the numeric value of a char, assuming up to base 36
 *  
 * @param ch The character to decode
 *
 * \return Numeric value of character, or 37 on failure
 */
static inline unsigned short
char_value(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'z') {
		return ch - 'a' + 10;
	}
	if (ch >= 'A' && ch <= 'Z') {
		return ch - 'A' + 10;
	}
	
	return 37;
}


long int
strtol(const char *nptr, char **endptr, int base)
{
	/*
	  Decompose input into three parts:
	  - initial list of whitespace (as per isspace)
	  - subject sequence
	  - final string one or more unrecognized
	*/
	const char *ptr = nptr;
	bool negative = false;
	unsigned int value;
	long int return_value = 0;
	/* Remove spaces */
	while(*ptr != '\0') {
		if (! isspace(*ptr)) {
			break;
		}
		ptr++;
	}

	if (*ptr == '\0') 
		goto fail;

	/* check [+|-] */	
	if (*ptr == '+') {
		ptr++;
	} else if (*ptr == '-') {
		negative = true;
		ptr++;
	}

	if (*ptr == '\0') 
		goto fail;

	if (base == 16) {
		/* _May_ have 0x prefix */
		if (*ptr == '0') {
			ptr++;
		        if (*ptr == 'x' || *ptr == 'X') {
				ptr++;
			}
		}
	}

	/* [0(x|X)+] */
	if (base == 0) {
		/* Could be hex or octal or decimal */
		if (*ptr != '0') {
			base = 10;
		} else {
			ptr++;
			if (ptr == '\0')
				goto fail;
			if (*ptr == 'x' || *ptr == 'X') {
				base = 16;
				ptr++;
			} else {
				base = 8;
			}
		}
	}

	if (*ptr == '\0')
		goto fail;

	/* Ok, here we have a base, and we might have a valid number */
	value = char_value(*ptr);
	if (value >= base) {
		goto fail;
	} else {
		return_value = value;
		ptr++;
	}

	while (*ptr != '\0' && (value = char_value(*ptr)) < base) {
		return_value = return_value * base + value;
		ptr++;
	}

	if (endptr != NULL)
		*endptr = (char*) ptr;

	if (negative) {
		return_value *= -1;
	}

	return return_value;

	/*
	  if base is 0, then we work it out based on a couple
	  of things 
	*/
	/*
	  [+|-][0(x|X)+][0-9A-Za-z] not LL *
	*/

	/* endptr == final string */

 fail:
	if (endptr != NULL)
		*endptr = (char*) nptr;
	return 0;

}

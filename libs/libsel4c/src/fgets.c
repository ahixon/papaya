/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <string.h>

char *
fgets(char *s, int n, FILE *stream)
{
	int i;
	int c = EOF;
	
	lock_stream(stream);
	for (i = 0; i < n-1; i++) {
		c = fgetc(stream);
		if (c == EOF) {
			break;
		}
		s[i] = (char) c;
		if (c == '\n') {
			break;
		}
	}
	unlock_stream(stream);

	s[i+1] = '\0';
	if (c == EOF && i == 0) {
		return NULL;
	} else {
		return s;
	}
}

/* @LICENSE(NICTA_CORE) */

#include <time.h>

time_t
time(time_t *timer)
{
	time_t val = 0;
	if (timer)
		*timer = val;
	return val;
}

/* @LICENSE(NICTA_CORE) */

#include <time.h>

double
difftime(time_t time1, time_t time2)
{
	return (double) (time1 - time2);
}

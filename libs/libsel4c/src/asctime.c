/* @LICENSE(NICTA_CORE) */

/*
 Authors: Ben Leslie
 Created: Fri Sep 24 2004 
 Note: Implementation taken verbatim from ISOC99 spec. page 341
*/
#include <time.h>
#include <stdio.h>

/* Not required to be thread safe by posix */
char *
asctime(const struct tm *timeptr) 
{ 
	static const char wday_name[7][3] = { 
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" 
	}; 
	static const char mon_name[12][3] = { 
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", 
		"Aug", "Sep", "Oct", "Nov", "Dec" 
	}; 
	
	static char result[26]; /* NOT THREAD SAFE */

	sprintf(result, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n", 
		wday_name[timeptr->tm_wday], mon_name[timeptr->tm_mon], 
		timeptr->tm_mday, timeptr->tm_hour, 
		timeptr->tm_min, timeptr->tm_sec, 
		1900 + timeptr->tm_year); 
	return result; 
} 

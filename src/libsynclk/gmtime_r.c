/*
   ** This file is in the public domain, so clarified as of
   ** 1996-06-05 by Arthur David Olson.
   */

/*
 * mktm_r.c
 * Original Author: Adapted from tzcode maintained by Arthur David Olson.
 * Modifications:       Changed to mktm_r and added __tzcalc_limits - 04/10/02, Jeff Johnston
 * Converts the calendar time pointed to by tim_p into a broken-down time
 * expressed as local time. Returns a pointer to a structure containing the
 * broken-down time.
 */

#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define SECSPERMIN	60
#define MINSPERHOUR	60
#define HOURSPERDAY	24
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	(SECSPERHOUR * HOURSPERDAY)
#define DAYSPERWEEK	7
#define MONSPERYEAR	12

#define YEAR_BASE  1900
#define EPOCH_YEAR 1970
#define EPOCH_WDAY 4

#define ISLEAP(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static const uint8_t mon_lengths[2][MONSPERYEAR] = {
	{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

static const uint16_t year_lengths[2] = { 365, 366 };

struct tm * symclk_gmtime_r(const time_t * tim_p, struct tm * res)
{
	long days, rem;
	time_t lcltime;
	int y;
	int yleap;
	const uint8_t * ip;
	int wday;

	/* base decision about std/dst time on current time */
	lcltime = * tim_p;

	days = ((long) lcltime) / SECSPERDAY;
	rem = ((long) lcltime) % SECSPERDAY;
	while (rem < 0) {
		rem += SECSPERDAY;
		--days;
	}
	while (rem >= SECSPERDAY) {
		rem -= SECSPERDAY;
		++days;
	}

	/* compute hour, min, and sec */
	res->tm_hour = (int) (rem / SECSPERHOUR);
	rem %= SECSPERHOUR;
	res->tm_min = (int) (rem / SECSPERMIN);
	res->tm_sec = (int) (rem % SECSPERMIN);

	/* compute day of week */
	wday = ((EPOCH_WDAY + days) % DAYSPERWEEK);
	if (wday < 0)
		res->tm_wday += DAYSPERWEEK;

	res->tm_wday = wday;

	/* compute year & day of year */
	y = EPOCH_YEAR;
	if (days >= 0) {
		for (;;) {
			yleap = ISLEAP(y);
			if (days < year_lengths[yleap])
				break;
			y++;
			days -= year_lengths[yleap];
		}
	} else {
		do {
			--y;
			yleap = ISLEAP(y);
			days += year_lengths[yleap];
		}
		while (days < 0);
	}

	res->tm_year = y - YEAR_BASE;
	res->tm_yday = days;
	ip = mon_lengths[yleap];
	for (res->tm_mon = 0; days >= ip[res->tm_mon]; ++res->tm_mon)
		days -= ip[res->tm_mon];
	res->tm_mday = days + 1;

	return (res);
}

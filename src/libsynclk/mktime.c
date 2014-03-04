/*
   ** This file is in the public domain, so clarified as of
   ** 1996-06-05 by Arthur David Olson.
   */

/*
 * mktime.c
 * Original Author:	G. Haley
 *
 * Converts the broken-down time, expressed as local time, in the structure
 * pointed to by __tm into a calendar time value. The original values of the
 * tm_wday and tm_yday fields of the structure are ignored, and the original
 * values of the other fields have no restrictions. On successful completion
 * the fields of the structure are set to represent the specified calendar
 * time. Returns the specified calendar time. If the calendar time can not be
 * represented, returns the value (time_t) -1.
 */

#include <stdlib.h>
#include <time.h>

#define _SEC_IN_MINUTE 60L
#define _SEC_IN_HOUR 3600L
#define _SEC_IN_DAY 86400L

static const char DAYS_IN_MONTH[12] =
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

#define _DAYS_IN_MONTH(x) ((x == 1) ? days_in_feb : DAYS_IN_MONTH[x])

static const short _DAYS_BEFORE_MONTH[12] =
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

#define _ISLEAP(y) (((y) % 4) == 0 && \
					(((y) % 100) != 0 || (((y)+1900) % 400) == 0))

#define _DAYS_IN_YEAR(year) (_ISLEAP(year) ? 366 : 365)

static void validate_structure(struct tm * __tm)
{
	div_t res;
	int days_in_feb = 28;

	/* calculate time & date to account for out of range values */
	if (__tm->tm_sec > 59) {
		res = div(__tm->tm_sec, 60);
		__tm->tm_min += res.quot;
		__tm->tm_sec = res.rem;
	}

	if (__tm->tm_min > 59) {
		res = div(__tm->tm_min, 60);
		__tm->tm_hour += res.quot;
		__tm->tm_min = res.rem;
	}

	if (__tm->tm_hour > 23) {
		res = div(__tm->tm_hour, 24);
		__tm->tm_mday += res.quot;
		__tm->tm_hour = res.rem;
	}

	if (__tm->tm_mon > 11) {
		res = div(__tm->tm_mon, 12);
		__tm->tm_year += res.quot;
		__tm->tm_mon = res.rem;
	}

	if (_DAYS_IN_YEAR(__tm->tm_year) == 366)
		days_in_feb = 29;

	if (__tm->tm_mday == 0) {
		if (__tm->tm_mon == 0) {
			__tm->tm_year--;
			__tm->tm_mon = 11;
			days_in_feb =
					((_DAYS_IN_YEAR(__tm->tm_year) == 366) ? 29 : 28);
		} else
			__tm->tm_mon--;

		__tm->tm_mday = _DAYS_IN_MONTH(__tm->tm_mon);

	} else {
		while (__tm->tm_mday > _DAYS_IN_MONTH(__tm->tm_mon)) {
			__tm->tm_mday -= _DAYS_IN_MONTH(__tm->tm_mon);
			if (++__tm->tm_mon == 12) {
				__tm->tm_year++;
				__tm->tm_mon = 0;
				days_in_feb =
					((_DAYS_IN_YEAR(__tm->tm_year) == 366) ? 29 : 28);
			}
		}
	}
}

time_t cs_mktime(struct tm *__tm)
{
	time_t tim = 0;
	long days = 0;
	int wday;
	int year, isdst;

	/* validate structure */
	validate_structure(__tm);

	/* compute hours, minutes, seconds */
	tim += __tm->tm_sec + (__tm->tm_min * _SEC_IN_MINUTE) +
		(__tm->tm_hour * _SEC_IN_HOUR);

	/* compute days in year */
	days += __tm->tm_mday - 1;
	days += _DAYS_BEFORE_MONTH[__tm->tm_mon];
	if (__tm->tm_mon > 1 && _DAYS_IN_YEAR(__tm->tm_year) == 366)
		days++;

	/* compute day of the year */
	__tm->tm_yday = days;

	if (__tm->tm_year > 208) {
		return (time_t) - 1;
	}

	/* compute days in other years */
	if (__tm->tm_year > 70) {
		for (year = 70; year < __tm->tm_year; year++)
			days += _DAYS_IN_YEAR(year);
	} else {
		if (__tm->tm_year < 70) {
			for (year = 69; year > __tm->tm_year; year--)
				days -= _DAYS_IN_YEAR(year);
			days -= _DAYS_IN_YEAR(year);
		}
	}

	/* compute day of the week */
	if ((wday = (days + 4) % 7) < 0)
		__tm->tm_wday = wday + 7;
	else
		__tm->tm_wday = wday;

	/* compute total seconds */
	tim += (days * _SEC_IN_DAY);

	isdst = __tm->tm_isdst;

#if 0
	if (_daylight) {
		int y = __tm->tm_year + YEAR_BASE;

		if (y == __tzyear || __tzcalc_limits(y)) {
			/* calculate start of dst in dst local time and 
			   start of std in both std local time and dst local time */
			time_t startdst_dst = __tzrule[0].change - __tzrule[1].offset;
			time_t startstd_dst = __tzrule[1].change - __tzrule[1].offset;
			time_t startstd_std = __tzrule[1].change - __tzrule[0].offset;

			/* if the time is in the overlap between dst and std local times */
			if (tim >= startstd_std && tim < startstd_dst);	/* we let user decide or leave as -1 */
			else {
				isdst = (__tznorth
						 ? (tim >= startdst_dst && tim < startstd_std)
						 : (tim >= startdst_dst || tim < startstd_std));
				/* if user committed and was wrong, perform correction */
				if ((isdst ^ __tm->tm_isdst) == 1) {
					/* we either subtract or add the difference between
					   time zone offsets, depending on which way the user got it wrong */
					int diff = __tzrule[0].offset - __tzrule[1].offset;

					if (!isdst)
						diff = -diff;
					__tm->tm_sec += diff;
					validate_structure(__tm);
					tim += diff;	/* we also need to correct our current time calculation */
				}
			}
		}
	}

	/* add appropriate offset to put time in gmt format */
	if (isdst == 1)
		tim += __tzrule[1].offset;
	else				/* otherwise assume std time */
		tim += __tzrule[0].offset;
#endif

	/* reset isdst flag to what we have calculated */
	__tm->tm_isdst = isdst;

	return tim;
}

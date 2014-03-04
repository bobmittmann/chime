/*
   Copyright(C) 2011 Robinson Mittmann.
  
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>

#include "debug.h"
#include "synclk.h"

/****************************************************************************
 * utility functions 
 ****************************************************************************/

int ts_sprint(char * s, uint64_t ts)
{
	time_t t;
	int sec;
	int min;
	int hour;
	int days;
	int ms;

	ms = ((ts & 0xffffffffLL) * 1000) >> 32;
	t = ts >> 32;
	min = t / 60;
	sec = t - (min * 60);
	hour = min / 60;
	min -= (hour * 60);
	days = hour / 24;
	hour -= (days * 24);

	return sprintf(s, "%2d:%02d:%02d.%03d", hour, min, sec, ms);
}

char * tsfmt(char * s, uint64_t ts)
{
	ts_sprint(s, ts);
	return s;
}

int frac_sprint(char * s, int32_t frac, int n)
{
	return sprintf(s, "%.6f", FRAC2D(frac));
}



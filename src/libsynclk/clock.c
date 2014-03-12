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

#ifndef CLOCK_DRIFT_MAX
//#define CLOCK_DRIFT_MAX FLOAT_Q31(0.000200)
/* FIXME: change the max drift to 200ppm */
#define CLOCK_DRIFT_MAX FLOAT_Q31(0.00200)
#endif

/****************************************************************************
 * Clock
 ****************************************************************************/

/* Update the clock time.
   This function should be called periodically by an
   interrupt handler. */
bool clock_tick(struct clock * clk)
{
	register uint64_t ts;
	register uint32_t sec;

	DBG5("tick");

	ts = clk->timestamp;
	sec = ts >> 32;
	clk->timestamp = ts += clk->increment;

	return ((ts >> 32) != sec) ? true : false;
}

/* Return the uncorrected (raw) clock timestamp */
uint64_t clock_timestamp(struct clock * clk) 
{
	register uint64_t ts;

	/* Disable interrupts */

	ts = clk->timestamp;

	/* Restore interrupts */

	return ts;
}

/* Return the corrected (offset) clock time */
uint64_t clock_time_get(struct clock * clk)
{
	register uint64_t ts;

	/* Disable interrupts */

	ts = clk->timestamp;
	ts += clk->offset;

	/* Restore interrupts */

	return ts;
}

/* Set the clock to the specified timestamp 'ts'. 
   This will adjust the clock offset only. */ 
void clock_time_set(struct clock * clk, uint64_t ts)
{
	/* Disable interrupts */
	clk->offset = (int64_t)(ts - clock_timestamp(clk));
	/* Restore interrupts */
}

/* Step the clock by the specified interval 'dt'. 
   This will adjust the clock offset only. */ 
void clock_step(struct clock * clk, int64_t dt)
{
	/* Disable interrupts */

	clk->offset += dt;

	/* Restore interrupts */
}

/* Adjust the clock frequency */ 
int32_t clock_drift_comp(struct clock * clk, int32_t drift)
{
	int32_t d;

	/* limit the maximum drift correction */
	if (drift > CLOCK_DRIFT_MAX)
		drift = CLOCK_DRIFT_MAX;
	else if (drift < -CLOCK_DRIFT_MAX)
		drift = -CLOCK_DRIFT_MAX;

	/* calculate the drift compenastion per tick */
	clk->drift_comp = Q31_MUL(drift, CLK_Q31(clk->resolution));
	d = + Q31_CLK(clk->drift_comp);

	/* Update the increpent per tick */
	clk->increment = clk->resolution + d;

	DBG4("FLL d=%d res=%.9f inc=%.9f", d, 
		CLK_FLOAT(clk->resolution), CLK_FLOAT(clk->increment));

	/* return the corrected drift adjustment */
	return clk->drift_comp * clk->frequency;
}

/* Initialize the clock */ 
void clock_init(struct clock * clk, uint32_t tick_freq_hz)
{
	unsigned int period_us;

	clk->timestamp = 0;
	clk->resolution = FLOAT_CLK(1.0 / tick_freq_hz);
	clk->frequency = tick_freq_hz;
	clk->drift_comp = 0;
	clk->increment = clk->resolution;
	/* Wed, 01 Jan 2014 00:00:00 GMT */
//	clk->offset = (uint64_t)1388534400LL << 32;  
	clk->offset = 0;
	clk->pps_flag = false;

	period_us = 1000000 / tick_freq_hz;
	(void)period_us;
	INF("period=%d.%03d ms, resolution=%u", 
		period_us / 1000, period_us % 1000, clk->resolution);
}



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
#define CLOCK_DRIFT_MAX FLOAT_Q31(0.000500)
#endif

#ifndef ENABLE_CLK_HIGH_RESOLUTION
#define ENABLE_CLK_HIGH_RESOLUTION 1
#endif

/****************************************************************************
 * Clock
 ****************************************************************************/

#if ENABLE_CLK_HIGH_RESOLUTION
/* Hardware timer frequency */
#define HW_TMR_FREQ_HZ 1000000
//#define HW_TMR_FREQ_HZ 110000000
#include "chime.h" 

uint64_t clock_timestamp(struct clock * clk) 
{
	register uint32_t cnt0;
	register uint32_t cnt1;
	register uint64_t ts;
	register uint32_t dt;

	do {
		cnt0 = chime_tmr_count(clk->hw_tmr);
		ts = clk->timestamp;
		cnt1 = chime_tmr_count(clk->hw_tmr);
	} while (cnt1 < cnt0);

	/* hardware timer ticks */
//	dt = ((uint64_t)clk->increment * cnt0 * clk->tmr_k + (1LL << 30)) >> 31;

	dt = (float)cnt0 * clk->increment * clk->tmr_fk;
	
	DBG5("cnt=%d k=%.12f dt=%.6f", cnt0, clk->tmr_fk, CLK_FLOAT(dt));

	DBG3("Fclk=%d.%06d cnt=%d inc=%.9f dt=%.9f", 
		 clk->n_freq, (int)Q31_MUL(clk->q_freq, 1000000), 
		 cnt1, CLK_FLOAT(clk->increment), CLK_FLOAT(dt)); 

	/* [sec] * [ticks] */

	if (dt != 0) {
		DBG5("Fclk=%d.%06d cnt=%d inc=%.9f dt=%.9f", 
			clk->n_freq, (int)Q31_MUL(clk->q_freq, 1000000), 
			cnt1, CLK_FLOAT(clk->increment), CLK_FLOAT(dt)); 
	}

	return ts + dt;
}

#else

/* Return the uncorrected (raw) clock timestamp */
uint64_t clock_timestamp(struct clock * clk) 
{
	register uint64_t ts;

	/* Disable interrupts */

	ts = clk->timestamp;

	/* Restore interrupts */

	return ts;
}

#endif

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

/* Return the corrected (offset) clock time */
uint64_t clock_time_get(struct clock * clk)
{
	register uint64_t ts;

	/* Disable interrupts */

	ts = clock_timestamp(clk);
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
int32_t clock_drift_comp(struct clock * clk, int32_t drift, int32_t est_err)
{
	register int32_t clk_d;
	register int32_t q31_d;

	/* limit the maximum drift correction */
	if (drift > CLOCK_DRIFT_MAX)
		drift = CLOCK_DRIFT_MAX;
	else if (drift < -CLOCK_DRIFT_MAX)
		drift = -CLOCK_DRIFT_MAX;

	/* average the estimated error */
	clk->jitter = (clk->jitter + est_err) / 2;


	clk_d = Q31_MUL(drift, clk->resolution);

	/* calculate the drift compenastion per tick */
//	clk->drift_comp = Q31_MUL(drift, CLK_Q31(clk->resolution));
//	clk_d = Q31_CLK(clk->drift_comp);

	/* update the drift compensation value */
	q31_d = CLK_Q31(clk_d);
	clk->drift_comp = q31_d;
	/* Update the increpent per tick */
	clk->increment = clk->resolution + clk_d;

	DBG4("FLL d=%d res=%.9f inc=%.9f", clk_d, 
		CLK_FLOAT(clk->resolution), CLK_FLOAT(clk->increment));

	/* return the corrected drift adjustment per second. 
	   Q31 fixed point format */
	return q31_d * clk->n_freq + Q31_MUL(q31_d, clk->q_freq);
}

int32_t clock_drift_get(struct clock * clk)
{
	register int32_t q31_d = clk->drift_comp;

	return q31_d * clk->n_freq + Q31_MUL(q31_d, clk->q_freq);
}

/* Initialize the clock */ 
void clock_init(struct clock * clk, uint32_t tick_itvl, int hw_tmr)
{
	unsigned int period_us;
	float freq_hz;

	freq_hz = 1.0 / CLK_FLOAT(tick_itvl);

	clk->timestamp = 0;
	clk->resolution = tick_itvl;
	clk->n_freq = freq_hz;
	clk->q_freq = FLOAT_Q31(freq_hz - clk->n_freq);
	clk->drift_comp = 0;
	clk->increment = clk->resolution;
	clk->tmr_k = (uint64_t)(1LL << 63) / ((int64_t)tick_itvl * HW_TMR_FREQ_HZ);
	clk->tmr_fk = freq_hz / HW_TMR_FREQ_HZ;
		
	INF("freq=%d.%06d Hz, k=%.12f", clk->n_freq, 
		(int)Q31_MUL(clk->q_freq, 1000000), clk->tmr_fk);

	/* Wed, 01 Jan 2014 00:00:00 GMT */
//	clk->offset = (uint64_t)1388534400LL << 32;  
	clk->offset = 0;
	clk->jitter = 0;
	clk->pps_flag = false;

	/* associated hardware timer */
	clk->hw_tmr = hw_tmr;

	period_us = 1000000 / freq_hz;
	(void)period_us;
	INF("period=%d.%03d ms, resolution=%.6f", 
		period_us / 1000, period_us % 1000, CLK_FLOAT(clk->resolution));

}



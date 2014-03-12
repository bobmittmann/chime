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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <inttypes.h>

#include "synclk.h"
#include "debug.h"

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) 
 ****************************************************************************/

/* edge filter window (seconds) */
#define FLL_EDGE_FILTER_WIN_MIN 120
#define FLL_EDGE_FILTER_WIN_MAX 900
/* Initial frequency calculation window (seconds) */
#define FLL_FREQ_CALC_MIN_WIN 1800

/* This is the actual FLL algorithm.
   It will adjust the frequency of the local clock 
   with a reference clock... */
void fll_step(struct clock_fll  * fll, uint64_t ref_ts, int64_t offs)
{
	char s[2][32];
	int64_t clk_dt;
	int64_t rtc_dt;
	int32_t drift;
	int64_t err;
	float freq;
	float dy;
	float dx;

	(void)s;

	do {
		/* compute the error between reference time and local clocks */
		err = ref_ts - clock_time_get(fll->clk);

		/* Sanity check. If we are off by too much, 
		   stop the FLL!!! */
		if ((err > fll->err_max) || (err < -fll->err_max)) {
			ERR("FLL error diff=%s ", fmt_clk(s[1], err));
			fll->err = err;
			if (fll->lock) {
				fll->lock = false;
				DBG("FLL (t=%d) unlocked at %s!", 
					CLK_SEC(ref_ts), fmt_clk(s[1], ref_ts));
			}
			break;
		}

		/* Eddge filter window to avoid detecting multiple transitions. */
		if (fll->edge_filt) {
			fll->edge_filt--;
			break;
		} 
	
		/* edge detection  */
		if (offs <= 0)
			break;

		/* As the clock rate is slower than the RTC clock
		   RTC_rate = 1 sec/sec (assumed to be corrrect)
		   CLK_rate = 1 / 1.000250 = 0.99975 sec/sec (for 250 ppm)
			offs = (RTC_time - CLK_time);
		  */

		/* Enable edge filter window to avoid detecting 
		   a second transition right after the first one. */
		fll->edge_filt = fll->edge_win;

		/* set the residual error to to be compensated by stepping
		 the local clock. */
		fll->err = err;

		if (!fll->run) {
			DBG1("err=%s offs=%s", fmt_clk(s[0], err), fmt_clk(s[1], offs));

			/* save the RTC timetamps */
			fll->ref_ts = ref_ts;
			/* save the raw local clock (no offset) */
			fll->clk_ts = clock_timestamp(fll->clk);
			/* save the edge polarity */
			fll->edge_offs = offs;

			/* make sure the lock falg is clear */
			fll->lock = false;
			/* change the state to RUN! */
			fll->run = true;
			DBG("FLL (t=%d) started at %s, edge=%.3f!", CLK_SEC(ref_ts), 
				fmt_clk(s[1], ref_ts), CLK_FLOAT(offs));
			break;
		} 

		/* Sanity check. If an edge is detected it should have the 
		   same polarity of the previous one */
		if (fll->edge_offs != offs) {
			ERR("FLL error: edge=%s offs=%s ", 
				fmt_clk(s[0], fll->edge_offs), 
				fmt_clk(s[1], offs));
			DBG("FLL (t=%d) stopped at %s, increasing edge window!", 
				CLK_SEC(ref_ts), fmt_clk(s[1], ref_ts));
			/* This is an indication of noise 
			   increase the window size */
			fll->edge_win = (fll->edge_win * 3) / 2; /* x 1.5 */
			if (fll->edge_win > FLL_EDGE_FILTER_WIN_MAX)
				fll->edge_win = FLL_EDGE_FILTER_WIN_MAX;
			fll->run = false;
			fll->edge_offs = 0;
			break;
		} 
		
		/* get raw local clock (no offset) */
		clk_dt = clock_timestamp(fll->clk) - fll->clk_ts;

		if (!fll->lock) {
			/* minimum window for initial frequency adjustment */
			if (clk_dt < FLOAT_CLK(FLL_FREQ_CALC_MIN_WIN))
				break;
			DBG("FLL locked at %s!", fmt_clk(s[1], ref_ts));
			fll->lock = true;
		} 

		/* update the timetamp for next round */
		fll->clk_ts += clk_dt;

		rtc_dt = ref_ts - fll->ref_ts;
		fll->ref_ts += rtc_dt;

		dy = CLK_FLOAT(clk_dt);
		dx = CLK_FLOAT(rtc_dt);

		freq = dy / dx;

		DBG4("FLL err=%s offs=%s dy=%.6f dx=%.6f", 
			 fmt_clk(s[0], err), fmt_clk(s[1], offs), dy, dx);

		DBG("FLL (t=%d) dx=%.1f err=%s drift=%0.9f", 
			CLK_SEC(ref_ts), dx, fmt_clk(s[0], err), 
			Q31_FLOAT(fll->drift));

		//	drift = (2 * fll->drift + FLOAT_Q31(1.0 - freq)) / 2;
		drift = fll->drift + FLOAT_Q31(1.0 - freq);
		fll->drift = clock_drift_comp(fll->clk, drift);

		DBG1("FLL freq=%.9f drift=%.9f", 
			 freq, Q31_FLOAT(fll->drift));
	} while (0);

	if (fll->err) {
		int32_t e;
		/* exponential amortization for the local clock error */
		e = fll->err / 128;
		if (e) {
			fll->err -= e;
			clock_step(fll->clk, e);
		}
	}
}

void fll_reset(struct clock_fll  * fll, uint64_t ref_ts)
{
	fll->run = false;
	fll->drift = 0;
	fll->err = 0;
	fll->edge_offs = 0;
	fll->edge_filt = 0;
	fll->edge_win = FLL_EDGE_FILTER_WIN_MIN;
	/* force clock to reference */
	clock_time_set(fll->clk, ref_ts);

}

void fll_init(struct clock_fll  * fll, struct clock  * clk, int64_t err_max)
{
	fll->clk = clk;
	fll->err_max = err_max;
	fll->run = false;
	fll->lock = false;
	fll->drift = 0;
	fll->err = 0;
	fll->edge_offs = 0;
	fll->edge_filt = 0;
	fll->edge_win = FLL_EDGE_FILTER_WIN_MIN;
}


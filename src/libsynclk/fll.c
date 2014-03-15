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
#include "chime.h"

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) 
 ****************************************************************************/

/* edge filter window (seconds) */
#define FLL_EDGE_FILTER_WIN_MIN 60
/* Initial frequency calculation window (seconds) */
#define FLL_FREQ_CALC_MIN_WIN 600

/* 
XXX: simulation; */
int fll_win_var;

/* FLL algorithm.
   It will adjust the frequency of the local clock to a reference clock... */
void fll_step(struct clock_fll  * fll, uint64_t ref_ts, int64_t offs)
{
	int64_t clk_dt;
	int64_t rtc_dt;
	int32_t drift;
	int32_t e_drift; /* drift error */
	int32_t d_drift; /* drift derivative */
	int64_t err;
	float freq;
	float dy;
	float dx;


	do {
		/* compute the error between reference time and local clocks */
		err = ref_ts - clock_time_get(fll->clk);

		/* Sanity check. If we are off by too much, 
		   stop the FLL!!! */
		if ((err > fll->err_max) || (err < -fll->err_max)) {
			ERR("FLL error diff=%s ", FMT_CLK(err));
			fll->clk_err = err;
			/* reset the edge filter */
			fll->edge_filt = 0;
			fll->edge_jit  = 0;
			if (fll->lock) {
				fll->lock = false;
				DBG("FLL (t=%d) unlocked at %s!", 
					CLK_SEC(ref_ts), FMT_CLK(ref_ts));
			}
			break;
		}

		/* Eddge filter window to avoid detecting multiple transitions. */
		if (fll->edge_filt) {
			if (offs < 0) { /* invalid edge detected  */
				/* extend the window */
				fll->edge_jit += FLL_EDGE_FILTER_WIN_MIN - fll->edge_filt;
				fll->edge_filt = FLL_EDGE_FILTER_WIN_MIN; 
	//			chime_var_rec(rtc_time_var, 1.4);
				break;
			}
			if (--fll->edge_filt == 0) {
				chime_var_rec(fll_win_var, 1.5);
				chime_var_rec(fll_win_var, 1.4);
				INF("FLL edge jitter: %d [secs]", fll->edge_jit);
			}
			break;
		} 
	
		/* As the clock rate is slower than the RTC clock
		   RTC_rate = 1 sec/sec (assumed to be corrrect)
		   CLK_rate = 1 / 1.000250 = 0.99975 sec/sec (for 250 ppm)
			offs = (RTC_time - CLK_time);
		  */
		/* edge detection  */
		if (offs <= 0)
			break;

		chime_var_rec(fll_win_var, 1.4);
		chime_var_rec(fll_win_var, 1.5);

		/* Enable edge filter window to avoid detecting 
		   a second transition right after the first one. */
		fll->edge_filt = FLL_EDGE_FILTER_WIN_MIN;
		fll->edge_jit  = 0;

		/* set the residual error to to be compensated by stepping
		 the local clock. */
		fll->clk_err = err;

		if (!fll->run) {
			DBG1("err=%s offs=%s", FMT_CLK(err), FMT_CLK(offs));

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
				FMT_CLK(ref_ts), CLK_FLOAT(offs));
			break;
		} 

		/* get raw local clock (no offset) */
		clk_dt = clock_timestamp(fll->clk) - fll->clk_ts;

		if (!fll->lock) {
			/* minimum window for initial frequency adjustment */
			if (clk_dt < FLOAT_CLK(FLL_FREQ_CALC_MIN_WIN))
				break;
			DBG("FLL locked at %s!", FMT_CLK(ref_ts));
			fll->lock = true;
		} 

		rtc_dt = ref_ts - fll->ref_ts;

		dy = CLK_FLOAT(clk_dt);
		dx = CLK_FLOAT(rtc_dt);

		freq = dy / dx;

		DBG4("FLL err=%s offs=%s dy=%.6f dx=%.6f", 
			 FMT_CLK(err), FMT_CLK(offs), dy, dx);

		DBG1("FLL (t=%d) dx=%.1f err=%s drift=%0.9f", 
			CLK_SEC(ref_ts), dx, FMT_CLK(err), Q31_FLOAT(fll->clk_drift));

		e_drift = FLOAT_Q31(1.0 - freq);
		d_drift = FLOAT_Q31(Q31_FLOAT(fll->drift_err - e_drift) / dx);
		drift = fll->clk_drift + e_drift + 16 * d_drift;
		// drift = (2 * fll->clk_drift + e_drift + 32 * d_drift) / 2;

		/* update the timetamps for next round */
		fll->clk_ts += clk_dt;
		fll->ref_ts += rtc_dt;
		/* update the drift error */
		fll->drift_err = e_drift;
		/* adjust the clock */
		fll->clk_drift = clock_drift_comp(fll->clk, drift, fll->clk_err);

		DBG("FLL freq=%.9f e_drift=%.9f d_drift=%.9f drift=%.9f ", 
			freq, Q31_FLOAT(e_drift), Q31_FLOAT(d_drift), 
			Q31_FLOAT(fll->clk_drift));

	} while (0);

	if (fll->clk_err) {
		int32_t e;
		/* exponential amortization for the local clock error */
		e = fll->clk_err / 128;
		if (e) {
			fll->clk_err -= e;
			clock_step(fll->clk, e);
		}
	}
}

void fll_reset(struct clock_fll  * fll, uint64_t ref_ts)
{
	fll->run = false;
	fll->clk_drift = 0;
	fll->clk_err = 0;
	fll->drift_err = 0;
	fll->edge_offs = 0;
	fll->edge_filt = 0;
	fll->edge_jit  = 0;
	/* force clock to reference */
	clock_time_set(fll->clk, ref_ts);

}

void fll_init(struct clock_fll  * fll, struct clock  * clk, int64_t err_max)
{
	fll->clk = clk;
	fll->err_max = err_max;
	fll->run = false;
	fll->lock = false;
	fll->clk_drift = 0;
	fll->clk_err = 0;
	fll->drift_err = 0;
	fll->edge_offs = 0;
	fll->edge_filt = 0;
	fll->edge_jit  = 0;
	{ /* XXX: simulation only!! */
		fll_win_var = chime_var_open("fll_win");
		chime_var_rec(fll_win_var, 1.4);
	}
}


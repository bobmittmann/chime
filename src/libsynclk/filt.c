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
#include "chime.h"

__thread int filt_avg_var;
__thread int filt_sigma_var;
__thread int filt_offs_var;

#define	CLOCK_PHI FLOAT_CLK(15e-6) /* max frequency error (s/s) */
#define FILT_OFFS_MAX FLOAT_CLK(0.1250)

static void __filt_clear(struct clock_filt * filt)
{
	int i;

	filt->spike = false;
	filt->offs = 0;
	filt->average = 0;
	filt->variance = CLK_MUL(FILT_OFFS_MAX, FILT_OFFS_MAX);
	filt->len = 0;
	filt->sx1 = 0;
	filt->sx2 = 0;

	for (i = 0; i < CLK_FILT_LEN; ++i) 
		filt->x[i] = 0;
}

static void __filt_stat_clear(struct clock_filt * filt)
{
	filt->stat.spike = 0;
	filt->stat.step = 0;
	filt->stat.drop = 0;
}

void filt_init(struct clock_filt * filt, struct clock  * clk)
{
	filt->clk = clk; 
	filt->precision = clk->resolution; 

	filt->peer.delay = 0;
	filt->peer.precision = 0;

	__filt_clear(filt);
	__filt_stat_clear(filt);

	/* open a simulation variable recorder */
	filt_avg_var = chime_var_open("filt_avg");
	filt_sigma_var = chime_var_open("filt_sigma");
	filt_offs_var = chime_var_open("filt_offs");
}


/* Reset the clock network filter.
   - peer_delay: average network delay (CLK format)
   - peer_precision: precison of this clock (CLK format) */
void filt_reset(struct clock_filt * filt, int32_t peer_delay, 
				int32_t peer_precision)
{
	filt->peer.delay  = peer_delay;
	filt->peer.precision = peer_precision;

	__filt_clear(filt);
	__filt_stat_clear(filt);

	DBG("<%d> delay=%s precision=%s", chime_cpu_id(), 
		FMT_CLK(peer_delay), FMT_CLK(peer_precision));
}


float __variance(int32_t offs[], int n)
{
	float sum1 = 0;
	float sum2 = 0;
	float mean = 0;
	float x;
	int i;

	for (i = 0; i < n; ++i) {
		x = Q31_FLOAT(offs[i]);
		sum1 = sum1 + x;
	}

	mean = sum1 / n;

	for (i = 0; i < n; ++i) {
		x = Q31_FLOAT(offs[i]);
		sum2 = sum2 + (x - mean) * (x - mean);
	}

	return sum2 / (n - 1);
}

float __online_variance(int64_t offs[], int len)
{
	float mean = 0;
	float m2 = 0;
	int n = 0;
	int i;

	for (i = 0; i < len; ++i)  {
		float x = CLK_FLOAT(offs[i]);
		float delta;
		n = n + 1;
		delta = x - mean;
		mean = mean + delta / n;
		m2 = m2 + delta * (x - mean);
	}

	return m2 / (n - 1);
}


/* Called when a time packed is received from the network */
int64_t filt_receive(struct clock_filt * filt, 
					 uint64_t remote, uint64_t local)
{
	int32_t delay;
	int64_t offs;
	int64_t disp;
	int64_t ret;
	register int32_t dx = 0;

	filt->peer.timestamp = remote;

	offs = (int64_t)(remote - local);
	delay = filt->peer.delay;
	offs += delay;
	disp = filt->precision + filt->peer.precision + CLOCK_PHI * delay;
	(void)disp;

	filt->offs = offs;

	ret = offs;

	if ((offs > FILT_OFFS_MAX) || (offs < -FILT_OFFS_MAX)) {
		if (filt->spike) {
			/* if we are in the SPIKE detect state, and 
			   the limits were excedded again, then step the clock. */
			WARN("step!!");
			filt->stat.step++;
			__filt_clear(filt);
		} else {
			WARN("spike!!");
			/* set the spike flag */
			filt->spike = true;
			/* do not update the clock */
			ret = CLK_OFFS_INVALID;
		}	
	} else {
		register int32_t x1;
		register int32_t x2;
		register int32_t y1;
		register int32_t y2;
		register int64_t sx1;
		register int32_t sx2;
		register int32_t avg;
		register int n;
		register int i;

		x1 = CLK_Q31(offs);

		n = filt->len;
		if (n < CLK_FILT_LEN) {
			filt->len = ++n;
		} else {
			dx = x1 - filt->average;
			if (Q31_MUL(dx, dx) > filt->variance * 4) {
				/* do not update the clock */
				ret = CLK_OFFS_INVALID;
				chime_var_rec(filt_avg_var, 1.9);
			}
		}

		y1 = filt->x[CLK_FILT_LEN - 1];
		for (i = (CLK_FILT_LEN - 1); i > 0; --i)
			filt->x[i] = filt->x[i - 1];
		filt->x[0] = x1;
		x2 = Q31_MUL(x1, x1); /* square */ 
		y2 = Q31_MUL(y1, y1); /* square */ 

		sx1 = filt->sx1;
		sx1 += x1 - y1;

		sx2 = filt->sx2;
		sx2 += x2 - y2;

		filt->sx1 = sx1;
		filt->sx2 = sx2;

		if (n < CLK_FILT_LEN) {
			filt->len = ++n;
			avg = sx1 / n;
		} else {
			register int32_t k = FLOAT_Q31(1.0 / (CLK_FILT_LEN - 1));
			/* mean */
			avg = sx1 / CLK_FILT_LEN;
			/* variance */
			/* v = (sx2 - (sx1**2) / n) / (n - 1); */
			filt->variance = Q31_MUL((sx2 - Q31_MUL(avg, sx1)), k);
		}

		/* mean */
		filt->average = avg;
	}

//	chime_var_rec(filt_mean_var, filt->stat.mean + 1.85);

//	chime_var_rec(filt_offs_var, CLK_FLOAT(filt->offs) + 1.85);
	chime_var_rec(filt_offs_var, Q31_FLOAT(dx) + 1.85);

//	chime_var_rec(filt_avgs_var, Q31_FLOAT(filt->average) + 1.85);

	chime_var_rec(filt_sigma_var, sqrt(Q31_FLOAT(filt->variance) * 3) + 1.85);

//	chime_var_rec(filt_avg_var, sqrt(__variance(filt->x, CLK_FILT_LEN)) + 1.85);
	chime_var_rec(filt_avg_var, CLK_FLOAT(filt->average) + 1.85);

	/* FIXME: implement filtering */
	DBG1("remote=%s local=%s offs=%s delay=%s", 
		FMT_CLK(remote), FMT_CLK(local), FMT_CLK(offs), FMT_CLK(delay));

	return ret;
}


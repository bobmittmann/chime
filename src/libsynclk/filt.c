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
__thread int filt_mean_var;

#define	CLOCK_PHI FLOAT_CLK(15e-6) /* max frequency error (s/s) */

void filt_init(struct clock_filt * filt, struct clock  * clk)
{
	filt->clk = clk; 
	filt->precision = clk->resolution; 

	filt->peer.precision = 0;
	filt->peer.delay = 0;

	filt->stat.variance = 0;
	filt->stat.mean = 0;
	filt->stat.n = 0;

	filt->avg = 0;
	filt->len = 0;

	/* open a simulation variable recorder */
	filt_mean_var = chime_var_open("filt_mean");
	filt_avg_var = chime_var_open("filt_avg");
}

/* Reset the clock network filter.
   - peer_delay: average network delay (CLK format)
   - peer_precision: precison of this clock (CLK format) */
void filt_reset(struct clock_filt * filt, int32_t peer_delay, 
				int32_t peer_precision)
{
	filt->peer.delay  = peer_delay;
	filt->peer.precision = peer_precision;

	filt->stat.variance = 0;
	filt->stat.mean = 0;
	filt->stat.n = 0;

	filt->avg = 0;
	filt->len = 0;

	filt->stat.variance = 0;
	filt->stat.mean = 0;
	filt->stat.n = 0;

	DBG("<%d> delay=%s precision=%s", chime_cpu_id(), 
		FMT_CLK(peer_delay), FMT_CLK(peer_precision));
}

int64_t __variance(struct clock_filt * filt, float x)
{
	float delta;

	filt->stat.n++;
	delta = x - filt->stat.mean;
	filt->stat.mean += delta / filt->stat.n;
	filt->stat.m2 = delta * (x - filt->stat.mean);

	filt->stat.variance = filt->stat.m2 / (filt->stat.n - 1);

	return filt->stat.variance;
}

float __two_pass_variance(int32_t offs[], int n)
{
	float sum1 = 0;
	float sum2 = 0;
	float mean = 0;
	float x;
	int i;

	for (i = 0; i < n; ++i) {
		x = CLK_FLOAT(offs[i]);
		sum1 = sum1 + x;
	}

	mean = sum1 / n;

	for (i = 0; i < n; ++i) {
		x = CLK_FLOAT(offs[i]);
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

#define FILT_OFFS_MAX FLOAT_CLK(0.1000)

uint32_t i64sqrt(uint64_t x)
{
	uint64_t rem = 0;
	uint64_t root = 0;
	int i;

	for (i = 0; i < 32; ++i) {
		root <<= 1;
		rem = ((rem << 2) + (x >> 62));
		x <<= 2;
		root++;
		if (root <= rem) {
			rem -= root;
			root++;
		} else
			root--;
	}

	return root >> 1;
}	

int32_t isqrt(uint32_t x)
{
	uint32_t rem = 0;
	uint32_t root = 0;
	int i;

	for (i = 0; i < 16; ++i) {
		root <<= 1;
		rem = ((rem << 2) + (x >> 30));
		x <<= 2;
		root++;
		if (root <= rem) {
			rem -= root;
			root++;
		} else
			root--;
	}

	return root >> 1;
}	

/* Called when a time packed is received from the network */
int64_t filt_receive(struct clock_filt * filt, 
					 uint64_t remote, uint64_t local)
{
	int32_t delay;
	int64_t offs;
	int64_t disp;
	int64_t ret;
	int32_t sigma = 0;
	int i;

	filt->peer.timestamp = remote;

	offs = (int64_t)(remote - local);
	delay = filt->peer.delay;
	offs += delay;
	disp = filt->precision + filt->peer.precision + CLOCK_PHI * delay;
	(void)disp;

	ret = offs;
	if ((offs >= FILT_OFFS_MAX) || (offs <= -FILT_OFFS_MAX)) {
		WARN("spike!!");
	} else {
		int32_t x1;
		int32_t y1;
		int64_t x2;
		int64_t y2;
		int64_t m;
		int64_t v;
		int n = filt->len;

		x1 = offs;
		x2 = ((int64_t)x1 * (int64_t)x1) >> 32;

		if (n < CLK_FILT_LEN) {
			n++;
			filt->len = n;
			y1 = 0;
		} else
			y1 = filt->x[CLK_FILT_LEN - 1];

		y2 = ((int64_t)y1 * (int64_t)y1) >> 32;

		for (i = (CLK_FILT_LEN - 1); i > 0; --i)
			filt->x[i] = filt->x[i - 1];
		filt->x[0] = x1;

		filt->sx1 += x1 - y1;
		filt->sx2 += x2 - y2;

		m = filt->sx1 / n;
		if (n > 1)  
			v = (n * filt->sx2 - ((filt->sx1 * filt->sx1) >> 32)) / 
				(n * (n - 1));
		else
			v = 0;

		filt->avg = m;
		filt->stat.variance = CLK_FLOAT(v);
		filt->stat.mean = CLK_FLOAT(m);

		if (filt->len < CLK_FILT_LEN) {
			/* Cumulative moving average */
			filt->avg = (filt->avg * (n - 1) + x1) / n;
			DBG1("C len=%d filt->avg=%s", filt->len, FMT_CLK(filt->avg));
		} else {

			/* moving average */
			filt->avg = filt->avg + (x1 - y1) / CLK_FILT_LEN;
			DBG1("M len=%d filt->avg=%s old=%s", filt->len, FMT_CLK(filt->avg), 
				FMT_CLK(y1));
		}

//		filt->stat.variance = __online_variance(filt->offs, filt->len);
		filt->stat.sigma = sqrt(CLK_FLOAT(v));

		sigma = FLOAT_CLK(sqrt(CLK_FLOAT(v)) * 1.5);

		if ((offs > sigma) || (offs < -sigma))
			ret = INT64_MAX;
	}

	filt->offs = offs;



//		filt->stat.variance = 0;
//		filt->stat.mean = offs;
//		filt->stat.n = 1;

//		filt->avg = offs;
//		filt->len = 1;


	chime_var_rec(filt_avg_var, CLK_FLOAT(filt->offs) + 1.85);
//	chime_var_rec(filt_mean_var, filt->stat.mean + 1.85);
	chime_var_rec(filt_mean_var, filt->stat.sigma+ 1.85);

	/* FIXME: implement filtering */
	DBG1("remote=%s local=%s offs=%s delay=%s", 
		FMT_CLK(remote), FMT_CLK(local), FMT_CLK(offs), FMT_CLK(delay));

	return ret;
}


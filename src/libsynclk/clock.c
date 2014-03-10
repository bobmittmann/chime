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

/****************************************************************************
 * System Clock
 ****************************************************************************/

bool clock_tick(struct clock * clk)
{
	uint64_t ts;
	uint32_t sec;

	DBG5("tick");

	ts = clk->timestamp;
	sec = ts >> 32;
	clk->timestamp = ts += clk->increment;

	if ((ts >> 32) != sec) {
		/* second update, signal the pps flag */
		clk->pps_flag = true;
		DBG1("PPS");
		return true;
	}

	return false;
}

__thread int offs_var;
__thread int err_var;
__thread int de_var;

static void __pll_reset(struct clock * clk)
{
	int i;

	clk->pll.err = 0;

	/* IIR order filer */
	for (i = 0; i < sizeof(clk->pll.f0.x) / sizeof(int32_t); ++i) {
		clk->pll.f0.x[i] = 0;
		clk->pll.f0.y[i] = 0;
	}

	for (i = 0; i < sizeof(clk->pll.f1.x) / sizeof(int32_t); ++i) {
		clk->pll.f1.x[i] = 0;
		clk->pll.f1.y[i] = 0;
	}
}


void clock_init(struct clock * clk, uint32_t tick_freq_hz)
{
	unsigned int period_us;

	clk->timestamp = 0;
	clk->resolution = (uint64_t)(1LL << 32) / tick_freq_hz;
	clk->frequency = tick_freq_hz >> 1;
	clk->drift_comp = 0;
	clk->increment = clk->resolution;
	/* Wed, 01 Jan 2014 00:00:00 GMT */
//	clk->offset = (uint64_t)1388534400LL << 32;  
	clk->offset = 0;
	clk->pps_flag = false;

	clk->pll.freq = 0;
	__pll_reset(clk);

	offs_var = chime_cpu_var_open("offset");
	err_var = chime_cpu_var_open("error");
	de_var = chime_cpu_var_open("deriv");

	period_us = 1000000 / tick_freq_hz;
	(void)period_us;
	INF("period=%d.%03d ms, resolution=%u", 
		period_us / 1000, period_us % 1000, clk->resolution);
}

uint64_t clock_timestamp(struct clock * clk)
{
	uint64_t ts;

	/* XXX: disable interrupts... */

	ts = clk->timestamp + clk->offset;

	/* XXX: restore interrupts... */

	return ts;
}

//#define PLL_B 0.5
//#define PLL_A 0

/* FC = 0.001953Hz, 512 sec */

/* FC= 0.000977Hz, 1024 sec */

#if 1
/* Bessel
   TS=32s TC=256s (0.001953Hz) */
#define PLL_A Q31(-0.41421)
#define PLL_B Q31(0.29289)
#endif


#if 0
/* Bessel
   TS=32s TC=512s (0.001953Hz) */
#define PLL_A Q31(-0.66818)
#define PLL_B Q31(0.16591)
#endif

#if 0
/* Bessel
   TS=32s TC=1024s (0.000977Hz) */
#define PLL_A Q31(-0.82068)
#define PLL_B Q31(0.089661)
#endif

#if 0
/* Bessel
   TS=32x TC=2048 (0.000488Hz) */
#define PLL_A Q31(-0.90635)
#define PLL_B Q31(0.046826)
#endif


#if 1
/* Bessel 
 TS=32s TC=1024s (0.000977Hz) */
#define PLL_A0 2
#define PLL_A1 Q31(-1.72378/PLL_A0)
#define PLL_A2 Q31(0.75755/PLL_A0)
#define PLL_B0 Q31(0.0084427/PLL_A0)
#define PLL_B1 Q31(0.0168854/PLL_A0)
#define PLL_B2 Q31(0.0084427/PLL_A0)
#endif

#if 0
/* Bessel 
 TS=32s TC=2048s (0.000488Hz) */
#define PLL_A0 2
#define PLL_A1 Q31(-1.86136/PLL_A0)
#define PLL_A2 Q31(0.87037/PLL_A0)
#define PLL_B0 Q31(0.0022516/PLL_A0)
#define PLL_B1 Q31(0.0045032/PLL_A0)
#define PLL_B2 Q31(0.0022516/PLL_A0)
#endif

#if 0
/* Bessel */
#define PLL_A0 1
#define PLL_A1 Q31(-0.87716188653003324000/PLL_A0)
#define PLL_A2 Q31(0.24043188698736320000/PLL_A0)
#define PLL_B0 Q31(0.09081750013497327800/PLL_A0)
#define PLL_B1 Q31(0.18163500026994656000/PLL_A0)
#define PLL_B2 Q31(0.09081750013497327800/PLL_A0)
#endif

#if 0
#define PLL_A0 1
#define PLL_A1 Q31(-0.98657618056564034/PLL_A0)
#define PLL_A2 Q31(0.44681268983470201/PLL_A0)
#define PLL_B0 Q31(0.11505882726663068/PLL_A0)
#define PLL_B1 Q31(0.23011765453326136/PLL_A0)
#define PLL_B2 Q31(0.11505882726663068/PLL_A0)
#endif

#if 0
#define PLL_A0 1
#define PLL_A1 Q31(-1.00000/PLL_A0)
#define PLL_A2 Q31(0.57406/PLL_A0)
#define PLL_B0 Q31(0.029955/PLL_A0)
#define PLL_B1 Q31(0.059909/PLL_A0)
#define PLL_B2 Q31(0.029955/PLL_A0)
#endif
/*
#define PLL_A0 2
#define PLL_A1 Q31(-1.45424/PLL_A0)
#define PLL_A2 Q31(0.57406/PLL_A0)
#define PLL_B0 Q31(0.029955/PLL_A0)
#define PLL_B1 Q31(0.059909/PLL_A0)
#define PLL_B2 Q31(0.029955/PLL_A0)

#define PLL_A0 1
#define PLL_A1 Q31(-0.94281/PLL_A0)
#define PLL_A2 Q31(0.33333/PLL_A0)
#define PLL_B0 Q31(0.097631/PLL_A0)
#define PLL_B1 Q31(0.195262/PLL_A0)
#define PLL_B2 Q31(0.097631/PLL_A0)

#define PLL_A0 1
#define PLL_A1 Q31(-0./PLL_A0)
#define PLL_A2 Q31(0.17157/PLL_A0)
#define PLL_B0 Q31(0.29289/PLL_A0)
#define PLL_B1 Q31(0.58579/PLL_A0)
#define PLL_B2 Q31(0.29289/PLL_A0)
*/

static int32_t iir_apply(int32_t x[], int32_t y[], int32_t v) 
{
	/* Shift the old samples */
	x[1] = x[0];
	y[1] = y[0];
	/* Calculate the new output */
	x[0] = v;
	y[0] = Q31MUL(PLL_B, x[0] + x[1]) - Q31MUL(PLL_A, y[1]);
	return y[0];
}

int32_t iir2_apply(int32_t x[], int32_t y[], int32_t v)
{
	int64_t y0;

	/* Shift the old samples */
	x[2] = x[1];
	x[1] = x[0];
	y[2] = y[1];
	y[1] = y[0];
	/* Calculate the new output */
	x[0] = v;
	y0 = Q31MUL(PLL_B0, x[0]);
	y0 += Q31MUL(PLL_B1, x[1]) - Q31MUL(PLL_A1, y[1]);
	y0 += Q31MUL(PLL_B2, x[2]) - Q31MUL(PLL_A2, y[2]);
	/* Scale and Truncate... */
	y[0] = PLL_A0 * y0;

	return y[0];
}

void clock_offs_adjust(struct clock * clk, int64_t offs_adj)
{
	DBG3("adjust=%"PRId64, offs_adj);
	DBG("A1=%f", Q31F(PLL_A1));
	clk->offset -= offs_adj;

	__pll_reset(clk);
}

#define PLL_PROP 0.004
#define PLL_DERIV 0.4
#define PLL_INTERVAL SYNCLK_POLL

#define PLL_DRIFT_MAX Q31(0.001000)

int32_t __pll_step(struct clock * clk, int32_t freq, int32_t offs)
{
	int32_t freq_adj;
	int32_t drift_adj;
	int32_t de;
	int32_t e;

	e = offs;
	e = iir_apply(clk->pll.f0.x, clk->pll.f0.y, offs);
	de = iir2_apply(clk->pll.f1.x, clk->pll.f1.y, (e - clk->pll.err));

	clk->pll.err = e;

//	chime_var_rec(offs_var, Q31F(offs));
//	chime_var_rec(err_var, Q31F(e));
//	chime_var_rec(de_var, Q31F(de));

	freq_adj = freq + (Q31MUL(e, Q31(PLL_PROP)) / PLL_INTERVAL) + 
		Q31MUL(de, Q31(PLL_DERIV * PLL_PROP));

	DBG("e=%.8f de=%.8f", Q31F(e), Q31F(de));

	if (freq_adj > PLL_DRIFT_MAX)
		freq_adj = PLL_DRIFT_MAX;
	else if (freq_adj < -PLL_DRIFT_MAX)
		freq_adj = -PLL_DRIFT_MAX;

	clk->pll.freq = freq_adj;
	clk->drift_comp = Q31MUL(clk->pll.freq, clk->resolution);

	clk->increment = clk->resolution - clk->drift_comp;
	drift_adj = clk->drift_comp * clk->frequency;

	DBG("freq=%.8f adj=%.8f comp=%d inc=%d.", 
		 Q31F(freq_adj), Q31F(drift_adj), clk->drift_comp, clk->increment);

	return drift_adj;
}

int32_t clock_phase_adjust(struct clock * clk, int32_t offs, 
						   int32_t interval)
{
	return __pll_step(clk, clk->pll.freq, offs);
}

int32_t clock_freq_adjust(struct clock * clk, int32_t freq_adj, int32_t offs)
{
	return __pll_step(clk, freq_adj, offs);
}

#define CLOCK_DRIFT_MAX Q31(0.01000)

int32_t clock_drift_adjust(struct clock * clk, int32_t freq_adj)
{
	/* limit the maximum drift correction */
	if (freq_adj > CLOCK_DRIFT_MAX)
		freq_adj = CLOCK_DRIFT_MAX;
	else if (freq_adj < -CLOCK_DRIFT_MAX)
		freq_adj = -CLOCK_DRIFT_MAX;

	/* calculate the drift compenastion per tick */
	clk->drift_comp = Q31MUL(freq_adj, clk->resolution);
	/* calculate the new increpent per tick */
	clk->increment = clk->resolution + clk->drift_comp;
	/* return the corrected drift adjustment */
	return clk->drift_comp * clk->frequency;
}

void clock_step(struct clock * clk, uint64_t ts)
{
	clk->offset = (int64_t)(ts - clk->timestamp);
}


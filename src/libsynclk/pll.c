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

__thread int pll_offs_var;
__thread int pll_err_var;
__thread int de_var;

#ifndef ENABLE_PLL_LOW_PASS
#define ENABLE_PLL_LOW_PASS 1
#endif

#define PLL_OFFS_MAX FLOAT_CLK(0.0625)


//#define PLL_B 0.5
//#define PLL_A 0

/* FC = 0.001953Hz, 512 sec */

/* FC= 0.000977Hz, 1024 sec */

#if 1
/* Bessel
   TS=32s TC=128s (0.001953Hz) */
#define PLL_A FLOAT_Q31(0.5)
#define PLL_B FLOAT_Q31(0.5)
#endif

#if 0
/* Bessel
   TS=32s TC=256s (0.001953Hz) */
#define PLL_A FLOAT_Q31(-0.41421)
#define PLL_B FLOAT_Q31(0.29289)
#endif


#if 0
/* Bessel
   TS=32s TC=512s (0.001953Hz) */
#define PLL_A FLOAT_Q31(-0.66818)
#define PLL_B FLOAT_Q31(0.16591)
#endif

#if 0
/* Bessel
   TS=32s TC=1024s (0.000977Hz) */
#define PLL_A FLOAT_Q31(-0.82068)
#define PLL_B FLOAT_Q31(0.089661)
#endif

#if 0
/* Bessel
   TS=32x TC=2048 (0.000488Hz) */
#define PLL_A FLOAT_Q31(-0.90635)
#define PLL_B FLOAT_Q31(0.046826)
#endif


#if 1
/* Bessel 
 TS=32s TC=1024s (0.000977Hz) */
#define PLL_A0 2
#define PLL_A1 FLOAT_Q31(-1.72378/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.75755/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.0084427/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.0168854/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.0084427/PLL_A0)
#endif

#if 0
/* Bessel 
 TS=32s TC=2048s (0.000488Hz) */
#define PLL_A0 2
#define PLL_A1 FLOAT_Q31(-1.86136/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.87037/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.0022516/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.0045032/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.0022516/PLL_A0)
#endif

#if 0
/* Bessel */
#define PLL_A0 1
#define PLL_A1 FLOAT_Q31(-0.87716188653003324000/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.24043188698736320000/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.09081750013497327800/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.18163500026994656000/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.09081750013497327800/PLL_A0)
#endif

#if 0
#define PLL_A0 1
#define PLL_A1 FLOAT_Q31(-0.98657618056564034/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.44681268983470201/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.11505882726663068/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.23011765453326136/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.11505882726663068/PLL_A0)
#endif

#if 0
#define PLL_A0 1
#define PLL_A1 FLOAT_Q31(-1.00000/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.57406/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.029955/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.059909/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.029955/PLL_A0)
#endif
/*
#define PLL_A0 2
#define PLL_A1 FLOAT_Q31(-1.45424/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.57406/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.029955/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.059909/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.029955/PLL_A0)

#define PLL_A0 1
#define PLL_A1 FLOAT_Q31(-0.94281/PLL_A0)
#define PLL_A2 FLOAT_Q31(0.33333/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.097631/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.195262/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.097631/PLL_A0)

#define PLL_A0 1
#define PLL_A1 FLOAT_Q31(-0./PLL_A0)
#define PLL_A2 FLOAT_Q31(0.17157/PLL_A0)
#define PLL_B0 FLOAT_Q31(0.29289/PLL_A0)
#define PLL_B1 FLOAT_Q31(0.58579/PLL_A0)
#define PLL_B2 FLOAT_Q31(0.29289/PLL_A0)
*/

#if ENABLE_PLL_LOW_PASS
static int32_t iir_apply(int32_t x[], int32_t y[], int32_t v) 
{
	/* Shift the old samples */
	x[1] = x[0];
	y[1] = y[0];
	/* Calculate the new output */
	x[0] = v;
	y[0] = Q31_MUL(PLL_B, x[0] + x[1]) - Q31_MUL(PLL_A, y[1]);
	return y[0];
}
#endif

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
	y0 = Q31_MUL(PLL_B0, x[0]);
	y0 += Q31_MUL(PLL_B1, x[1]) - Q31_MUL(PLL_A1, y[1]);
	y0 += Q31_MUL(PLL_B2, x[2]) - Q31_MUL(PLL_A2, y[2]);
	/* Scale and Truncate... */
	y[0] = PLL_A0 * y0;

	return y[0];
}

#define PLL_KD 4
#define PLL_KP 8
#define PLL_KI 512

#if 0
void pll_step(struct clock_pll  * pll)
{
	int32_t ierr;
	int32_t err;

	pll->ref = pll->ref - pll->ref / PLL_KD;
	err = CLK_Q31(pll->offs - pll->ref);

	pll->err = err;
	
	err /= PLL_KP;

	/* integral term */
	ierr = pll->ierr + err / PLL_KI;
	pll->ierr = ierr;

	pll->drift = clock_drift_comp(pll->clk, ierr + err, err);
	pll->offs -= Q31_CLK(err);
	
	chime_var_rec(pll_offs_var, CLK_FLOAT(pll->offs) + 1.7);
	chime_var_rec(pll_err_var, CLK_FLOAT(pll->err) + 1.65);

	return;
}
#endif

void pll_step(struct clock_pll  * pll)
{
	int32_t ierr;
	int32_t err;

	pll->ref = pll->ref - pll->ref / PLL_KD;
	err = pll->offs - pll->ref;

	pll->err = err;
	
	err /= PLL_KP;

	/* integral term */
	ierr = pll->ierr + err / PLL_KI;
	pll->ierr = ierr;

	pll->drift = clock_drift_comp(ierr + err, err);
	pll->offs -= err;
	
	chime_var_rec(pll_offs_var, Q31_FLOAT(pll->offs) + 1.7);
	chime_var_rec(pll_err_var, Q31_FLOAT(pll->err) + 1.65);

	return;
}

void pll_phase_adjust(struct clock_pll  * pll, int64_t offs, int64_t itvl)
{
	int32_t x;
	float dt;
	(void)dt;

	if ((offs >= PLL_OFFS_MAX) || (offs <= -PLL_OFFS_MAX)) {
		WARN("clock_step()!!");
		/* force clock to reference */
		clock_step(offs);
		pll->offs = 0;
		pll->ref = 0;
		return;
	}

	pll->itvl = itvl;

	x = CLK_Q31(offs);
//	x = iir2_apply(pll->f1.x, pll->f1.y, offs);
#if ENABLE_PLL_LOW_PASS
	x = iir_apply(pll->f0.x, pll->f0.y, x);
#endif
	pll->offs = x;
	pll->ref = x;

	dt = CLK_FLOAT(offs) / CLK_FLOAT(itvl);

	DBG("offs=%s itvl=%s dt=%.6f", FMT_CLK(pll->offs), FMT_CLK(pll->itvl), dt);
}

void pll_reset(struct clock_pll  * pll)
{
	int i;

	pll->run = false;
	pll->lock = false;
	pll->drift = 0;
	pll->offs = 0;
	pll->err = 0;
	pll->ref = 0;

	/* IIR order filer */
	for (i = 0; i < sizeof(pll->f0.x) / sizeof(int32_t); ++i) {
		pll->f0.x[i] = 0;
		pll->f0.y[i] = 0;
	}

	for (i = 0; i < sizeof(pll->f1.x) / sizeof(int32_t); ++i) {
		pll->f1.x[i] = 0;
		pll->f1.y[i] = 0;
	}

}

void pll_init(struct clock_pll  * pll)
{
	pll->run = false;
	pll->lock = false;
	pll->drift = 0;
	pll->offs = 0;
	pll->err = 0;
	pll->ref = 0;

	/* open a simulation variable recorder */
	pll_offs_var = chime_var_open("pll_offs");
	pll_err_var = chime_var_open("pll_err");
}


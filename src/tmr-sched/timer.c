/*
 * @file	net-queue.c
 * @brief	Priority queue
 * @author	Robinson Mittmann (bobmittmann@gmail.com)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "debug.h"
#include "timer.h"
#include "chime.h"

/****************************************************************************
  Timer schedule
 ****************************************************************************/

#define TIMER_SCHED_LENGHT 64

struct tmr_sched {
	volatile bool wakeup;
	uint32_t clk;
	uint32_t tmo_clk;
	struct timer tmr[TIMER_SCHED_LENGHT];
};

struct tmr_sched sched;

void timer_sched_isr(void)
{
	register uint32_t clk;

	clk = sched.clk + 1;
	sched.clk = clk;

	DBG5("<%d>", (int32_t)(sched.tmo_clk - clk));

	if ((int32_t)(sched.tmo_clk - clk) <= 0) {
		DBG5("wakeup...");
		/* move next timeout to infinity :) */
		sched.tmo_clk = clk + INT32_MAX;
		/* wakeup worker thread */
		sched.wakeup = true;
	}
}

void timer_default_callback(void * param)
{
	DBG("...");
}

bool timer_init(unsigned int tmr_id, void (* callback)(void *), void * param)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return false;
	}

	tmr = &sched.tmr[tmr_id];

	DBG("tmr_id=%d", tmr_id);

	tmr->clk = 0;
	tmr->itval = 0;
	tmr->enabled = false;
	if (callback == NULL)
		callback = timer_default_callback;
	tmr->callback = callback;
	tmr->param = param;

	return true;
}

static inline void timer_reload(struct timer * tmr, uint32_t clk)
{
	tmr->clk = clk;
	if ((int32_t)(clk - sched.tmo_clk) < 0) {
		DBG("tmo=%d", clk);
		sched.tmo_clk = clk;
	}
}

void timer_set(unsigned int tmr_id, uint32_t timeout, uint32_t period)
{
	register uint32_t clk;
	register struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];

	tmr->itval = period;

	DBG("tmr_id=%d tmo=%d itv=%d clk=%d", tmr_id, timeout, period, tmr->clk);

	clk = sched.clk + timeout;

	tmr->clk = clk;
	if ((int32_t)(clk - sched.tmo_clk) < 0) {
		DBG5("tmo=%d", clk);
		sched.tmo_clk = clk;
	}

	tmr->enabled = true;
}

void timer_start(unsigned int tmr_id)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];
	tmr->enabled = true;
}

void timer_stop(unsigned int tmr_id)
{
	struct timer * tmr;

	assert(tmr_id < TIMER_SCHED_LENGHT);

	tmr = &sched.tmr[tmr_id];

	tmr->enabled = false;
}

void timer_sched(void)
{
	register int32_t dt_min = INT32_MAX;
	register uint32_t ref_clk;
	register int32_t dt;
	register int i;

	if (!sched.wakeup)
		return;

	sched.wakeup = false;
	ref_clk = sched.clk;

	for (i = 0; i < TIMER_SCHED_LENGHT; ++i) {
		register struct timer * tmr = &sched.tmr[i];

		if (!tmr->enabled) 
			continue;

		/* process timer */
		if ((dt = (int32_t)(tmr->clk - ref_clk)) <= 0) {
			if (tmr->itval) {
				register uint32_t clk = tmr->clk + tmr->itval;
				/* reschedule periodic timer */
				tmr->clk = clk;
				if ((int32_t)(clk - sched.tmo_clk) < 0) {
					DBG5("tmo=%d", clk);
					sched.tmo_clk = clk;
				}

			} else {
				/* disable nonperiodic timer */
				tmr->enabled = false;
			}

			tmr->callback(tmr->param);
		} else {
			if (dt < dt_min)
				dt_min = dt;
		}
	}

	ref_clk += dt_min;
	if ((int32_t)(ref_clk - sched.tmo_clk) < 0) {
		DBG5("tmo=%d", ref_clk);
		sched.tmo_clk = ref_clk;
	}
}

void __timer_sched(void)
{
	register int32_t dt_min = INT32_MAX;
	register uint32_t ref_clk;
	register int32_t dt;
	register int i;

	if (!sched.wakeup)
		return;

	sched.wakeup = false;
	ref_clk = sched.clk;
	
	for (i = 0; i < TIMER_SCHED_LENGHT; ++i) {
		register struct timer * tmr = &sched.tmr[i];

		while (tmr->enabled) {
			if ((dt = (int32_t)(tmr->clk - ref_clk)) <= 0) {
				if (tmr->itval) {
					/* reschedule a periodic clock */
					tmr->clk = ref_clk + tmr->itval;
				} else {
					/* disable the timer */
					tmr->enabled = false;
				}
				tmr->callback(tmr->param);
			} else {
				if (dt < dt_min)
					dt_min = dt;
				break;
			}
		}
	}

	ref_clk += dt_min;
	if ((int32_t)(ref_clk - sched.tmo_clk) < 0) {
		DBG5("tmo=%d", ref_clk);
		sched.tmo_clk = ref_clk;
	}
}

void timer_sched_init(void)
{
	unsigned int i;

	for (i = 0; i < TIMER_SCHED_LENGHT; ++i) {
		sched.tmr[i].enabled = false;
		sched.tmr[i].clk = 0;
		sched.tmr[i].itval = 0;
		sched.tmr[i].callback = timer_default_callback;
		sched.tmr[i].param = NULL;
	}

	sched.wakeup = false;
	sched.clk = 0;
	sched.tmo_clk = INT32_MAX;

	chime_tmr_init(7, timer_sched_isr, 1000, 1000);
}


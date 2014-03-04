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
	sched.clk++;

	if ((sched.tmo_clk - sched.clk) < 0) {
		DBG("wakeup...");
		/* wakeup worker thread */
		sched.wakeup = true;
	}
}

void timer_default_callback(void * param)
{
	DBG("...");
}

void timer_init(unsigned int tmr_id, void (* callback)(void *), void * param)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
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
}

void timer_set(unsigned int tmr_id, uint32_t timeout, uint32_t period)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];

	tmr->itval = period;
	tmr->clk = sched.clk + timeout;

	DBG("tmr_id=%d tmo=%d itv=%d clk=%d", tmr_id, timeout, period, tmr->clk);

	if ((tmr->clk - sched.tmo_clk) < 0)
		sched.tmo_clk = tmr->clk;

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
	int32_t dt_min = INT32_MAX;
	struct timer * tmr;
	uint32_t clk;
	int32_t dt;
	int i;

	if (!sched.wakeup)
		return;

	sched.wakeup = false;
	clk = sched.tmo_clk;
	
	for (i = 0; i < TIMER_SCHED_LENGHT; ++i) {

		tmr = &sched.tmr[i];

		while (tmr->enabled) {
			if ((dt = tmr->clk - clk) <= 0) {
				if (tmr->itval) {
					/* reschedule a periodic clock */
					tmr->clk = clk + tmr->itval;
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

	/* reschedule the clock */
	sched.tmo_clk = clk + dt_min;
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

	chime_tmr_init(7, timer_sched_isr, 1000, 1000);
}


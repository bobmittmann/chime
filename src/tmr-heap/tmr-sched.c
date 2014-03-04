#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"
#include "timer.h"

void timer_0_cb(void * param)
{
	INF("<%d>", chime_cpu_id());
}

void timer_1_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer_2_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer_3_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer_4_cb(void * param)
{
	INF("<%d>", chime_cpu_id());
}

void timer_5_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer_6_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer_7_cb(void * param)
{
	INF("<%d> ...", chime_cpu_id());
}

void cpu0_reset(void)
{

	timer_sched_init();
	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	timer_init(0, timer_0_cb, NULL);
	timer_init(1, timer_1_cb, NULL);
	timer_init(2, timer_2_cb, NULL);
	timer_init(3, timer_3_cb, NULL);
	timer_init(4, timer_4_cb, NULL);
	timer_init(5, timer_5_cb, NULL);
	timer_init(6, timer_6_cb, NULL);
	timer_init(7, timer_7_cb, NULL);
	timer_init(8, timer_0_cb, NULL);
	timer_init(9, timer_1_cb, NULL);
	timer_init(10, timer_2_cb, NULL);
	timer_init(11, timer_3_cb, NULL);
	timer_init(12, timer_4_cb, NULL);
	timer_init(13, timer_5_cb, NULL);
	timer_init(14, timer_6_cb, NULL);
	timer_init(15, timer_7_cb, NULL);
	timer_init(16, timer_0_cb, NULL);

	timer_set(0, 100, 100);
	timer_set(1, 150, 150);
	timer_set(2, 300, 300);
	timer_set(3, 450, 450);
	timer_set(4, 440, 100);
	timer_set(5, 430, 150);
	timer_set(6, 420, 300);
	timer_set(7, 410, 450);
	timer_set(8, 10, 100);
	timer_set(9, 15, 150);
	timer_set(10, 30, 300);
	timer_set(11, 45, 450);
	timer_set(12, 44, 100);
	timer_set(13, 43, 150);
	timer_set(14, 42, 300);
	timer_set(15, 41, 450);
	timer_set(16, 1, 2);

	tmr_heap_dump(stdout);
	tmr_heap_flush(stdout);

	for (;;) {
		chime_cpu_wait();
		timer_sched();
	}
}


int main(int argc, char *argv[]) 
{
	printf("\n==== Timer scheduler test! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");	
		fflush(stderr);	
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	chime_cpu_create(0, 0, cpu0_reset);

	chime_reset_all();

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}

#if 0
void timer_isr(void)
{
}

void timer_task(void)
{
	struct timer_entry e;

	for (;;) {
		__sem_wait(tm_mgr.sem);
		__mutex_lock(tm_mgr.mtx);

		while (clk_heap_get_min(tm_mgr.heap, &e))  {

			if ((int32_t)(e.clk - tm_mgr.clk) > 0) {
				/* reschedule the timer */
				tm_mgr.tmr_clk = e.clk;
				break;
			}

			clk_heap_delete_min(tm_mgr.heap);
		
			if (e.tmr->interval) {
				/* reschedule the timer */
				e.clk += e.tmr->interval;
				clk_heap_insert(tm_mgr.heap, e);
			}

			__mutex_unlock(tm_mgr.mtx);
			e.tmr->callback(e.tmr->param);
			__mutex_lock(tm_mgr.mtx);

		}
		__mutex_unlock(tm_mgr.mtx);
	}
}

void timer_schedule(struct timer * tmr, uint32_t delay)
{
	struct timer_entry e;

	e.tmr = tmr;
	e.clk = etm_mgr.clk + delay;
	clk_heap_insert(tm_mgr.heap, e);

	if ((int32_t)(e.clk - tm_mgr.tmr_clk) < 0) 
		tm_mgr.tmr_clk = e.clk;
}

void timer_init(struct timer * tmr, uint32_t delay, 
				uint32_t interval, void (* callback)(void *),
				void * param)
{
	tmr->callback = callback;
	tmr->param = param;
	tmr->interval = interval;
	timer_schedule(tmr, delay);
}

#endif


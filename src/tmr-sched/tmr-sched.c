#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"
#include "timer.h"

struct tmr_test {
	unsigned int tmr_id;
	uint32_t clk;
	uint32_t clk_tmo;
	uint32_t itv;
	uint32_t cnt;
};

void timer_test_callback(struct tmr_test * tst)
{
	uint32_t clk;


	clk = chime_cpu_cycles();

	INF("[%d]", tst->tmr_id);

	if (tst->clk_tmo != clk) {
		fprintf(stderr, "[%d] Error!\n", tst->tmr_id);
		fflush(stderr);
	}

	tst->cnt++;
	tst->clk = clk;
	tst->clk_tmo = tst->clk + tst->itv;
}

void cpu0_reset(void)
{
	struct tmr_test tst[1024];
	int n;
	int i;

	timer_sched_init();
	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (i = 0; i < 1024; ++i) {
		tst[i].tmr_id = i;
		if (!timer_init(i, (void (*)(void *))timer_test_callback, &tst[i]))
			break;
	}
	n = i;

	for (i = 0; i < n; ++i) {
		uint32_t tmo = rand() % 2000;
		uint32_t itv = rand() % 2000;

		tst[i].clk = chime_cpu_cycles();
		tst[i].clk_tmo = tst[i].clk + tmo * 1000;
		tst[i].itv = itv * 1000;

		tracef(T_INF, "TMR %d: TMO=%d ITV=%d", tst[i].tmr_id, tmo, itv);

		timer_set(tst[i].tmr_id, tmo, itv);
	}

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


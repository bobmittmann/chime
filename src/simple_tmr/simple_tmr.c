#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"

static int timer1_delay;

void timer0_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
	timer1_delay += 100000 /* 100 ms */;
	tracef(T_INF, "CPU %d - Timer 0 timeout. Timer 1 reset.", 
		   chime_cpu_id());
	chime_tmr_reset(1, timer1_delay, 0);
}

void timer1_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
	tracef(T_INF, "CPU %d - Timer 1 timeout.", chime_cpu_id());
}

static int timer2_delay;

void timer2_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
	tracef(T_INF, "CPU %d - Timer 2 timeout: %d ms.", 
		   chime_cpu_id(), timer2_delay / 1000);
	timer2_delay += 100000; /* add 100 ms */
	chime_tmr_init(2, timer2_isr, timer2_delay, 0);
}

void timer3_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
	tracef(T_INF, "CPU %d - Timer 3 timeout. Single Shot!", chime_cpu_id());
}

void cpu0_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	timer1_delay = 100000;
	chime_tmr_init(1, timer1_isr, timer1_delay, 0);
	timer2_delay = 1500000;
	chime_tmr_init(2, timer2_isr, timer2_delay, 0);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

int main(int argc, char *argv[]) 
{
	printf("\n==== Simple simulation client ! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");	
		fflush(stderr);	
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	chime_cpu_create(0, 0, cpu0_reset);

	chime_reset_all();

	for (;;) {
		chime_sleep(10);
	}

	return 0;
}


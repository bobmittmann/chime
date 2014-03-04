#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"

static unsigned int tmr0_cnt;

void timer0_isr(void)
{
	tracef(T_DBG, "Timer-0: %d", tmr0_cnt++);
}

static unsigned int tmr1_cnt;

void timer1_isr(void)
{
	tracef(T_DBG, "Timer-1: %d", tmr1_cnt++);
}

void cpu0_reset(void)
{
	printf("Running...\n");
	fflush(stdout);

	/* initialize variables */
	tmr0_cnt = 0;
	tmr1_cnt = 0;

	/* initialize a timer to start in 2 seconds with 1 secont interval */
	chime_tmr_init(0, timer0_isr, 2000000, 1000000);
	/* initialize a timer to start in 2.5 seconds with 1 secont interval */
	chime_tmr_init(1, timer1_isr, 2500000, 1000000);

	/* send a trace message to the simulator */
	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

int main(int argc, char *argv[]) 
{
	printf("\n==== Single CPU simulation client ! ====\n");
	printf("\nReset the server to start...\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "Errror: Failed to connect to simulation server!\n");	
		fflush(stderr);
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	chime_cpu_run(0, 0, cpu0_reset);

	return 0;
}


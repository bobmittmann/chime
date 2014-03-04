#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"

void timer0_isr(void)
{
	static __thread uint32_t cnt = 0;

	INF("<%d> cnt=%d.", chime_cpu_id(), ++cnt);
	tracef(T_INF, "cnt=%d", cnt);
}

void timer1_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer2_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer3_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer4_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void cpu0_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	chime_tmr_init(1, timer1_isr, 1200000, 1000000);
	chime_tmr_init(2, timer2_isr, 1400000, 1000000);
	chime_tmr_init(3, timer3_isr, 1600000, 1000000);
	chime_tmr_init(4, timer4_isr, 1800000, 1000000);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu1_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	chime_tmr_init(1, timer1_isr, 1200000, 1000000);
	chime_tmr_init(2, timer2_isr, 1400000, 1000000);
	chime_tmr_init(3, timer3_isr, 1600000, 1000000);
	chime_tmr_init(4, timer4_isr, 1800000, 1000000);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu2_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	chime_tmr_init(1, timer1_isr, 1200000, 1000000);
	chime_tmr_init(2, timer2_isr, 1400000, 1000000);
	chime_tmr_init(3, timer3_isr, 1600000, 1000000);
	chime_tmr_init(4, timer4_isr, 1800000, 1000000);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu3_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	chime_tmr_init(1, timer1_isr, 1200000, 1000000);
	chime_tmr_init(2, timer2_isr, 1400000, 1000000);
	chime_tmr_init(3, timer3_isr, 1600000, 1000000);
	chime_tmr_init(4, timer4_isr, 1800000, 1000000);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu4_reset(void)
{
	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
	chime_tmr_init(1, timer1_isr, 1200000, 1000000);
	chime_tmr_init(2, timer2_isr, 1400000, 1000000);
	chime_tmr_init(3, timer3_isr, 1600000, 1000000);
	chime_tmr_init(4, timer4_isr, 1800000, 1000000);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu5_reset(void)
{
	chime_tmr_init(0, timer0_isr, 10000, 100);

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

//	chime_cpu_create(0, 0, cpu0_reset);

//	chime_cpu_create(1000, 0, cpu1_reset);

//	chime_cpu_create(2000, 0, cpu2_reset);

//	chime_cpu_create(3000, 0, cpu3_reset);

//	chime_client_stop();

//	chime_cpu_create(4000, 0, cpu4_reset);

	chime_cpu_create(0, 0, cpu5_reset);

	chime_reset_all();

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}


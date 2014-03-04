/*
   test.c
   IRPC main function
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

#include "chime.h"
#include "debug.h"

#define ARCNET_COMM 0

void timer0_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void timer1_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void arcnet_rcv_isr(void)
{
	char msg[1024];
	int len;

	len = chime_comm_read(ARCNET_COMM, msg, 1024);
	(void)len;

//	INF("<%d> ...", chime_cpu_id());

//	printf("%s: <%d> len=%d msg=\"%s\".\n", 
//		   __func__, chime_cpu_id(), len, msg);
//	fflush(stdout);
}

void cpu_slave_reset(void)
{
	tracef(T_DBG, "Slave reset...");

	printf("CPU:%d \"%s\", reset...\n", chime_cpu_id(), chime_cpu_name());
	fflush(stdout);

//	chime_tmr_init(0, timer0_isr, 1000000, 1000000);
//	chime_tmr_init(1, timer1_isr, 4000000, 2000000);
	chime_comm_attach(ARCNET_COMM, "ARCnet", arcnet_rcv_isr, NULL);

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu_master_reset(void)
{
//	int n = 0;
	char msg[] = "Hello world!";
	
	printf("CPU:%d \"%s\", reset...\n", chime_cpu_id(), chime_cpu_name());
	fflush(stdout);

//	chime_tmr_init(0, timer_isr, 1000000, 1000000);

	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL);

	tracef(T_DBG, "Master reset...");

	for (;;) {
		chime_cpu_step(250000);
//		printf("- xmt %d\n", ++n);
//		fflush(stdout);
		chime_comm_write(ARCNET_COMM, msg, sizeof(msg));
	}
}

void cpu_slave2_reset(void)
{
	uint32_t cycles;
	uint32_t old;
	int n = 0;
	
	printf("CPU:%d \"%s\", reset...\n", chime_cpu_id(), chime_cpu_name());
	fflush(stdout);

	chime_tmr_init(0, timer0_isr, 1000000, 1000000);

	tracef(T_DBG, "Reset...");

	cycles = chime_cpu_cycles();
	old = cycles;
	for (;;) {
		n++;
//		chime_cpu_wait();
		chime_cpu_step(100000);
		cycles = chime_cpu_cycles();
		if ((cycles - old) >= 1000000) {
			old = cycles;
			tracef(T_DBG, "clk=%d loop=%d", cycles / 1000, n);
		}
	}
}


void cleanup(void)
{
	chime_client_stop();
}

void net_init(void)
{
	struct comm_attr attr = {
		.wr_cyc_per_byte = 2,
		.wr_cyc_overhead = 4,
		.rd_cyc_per_byte = 2,
		.rd_cyc_overhead = 4,
		.bits_overhead = 0,
		.bits_per_byte = 10,
		.speed_bps = 9600, /* bits per second */
		.jitter = 0.1, /* seconds */
		.delay = 0.1  /* minimum delay in seconds */
	};

	if (chime_comm_create("ARCnet", &attr) < 0) {
		fprintf(stderr, "chime_comm_create() failed!\n");	
		fflush(stderr);	
	}
};

int main(int argc, char *argv[]) 
{
	printf("\n==== Clock server! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");	
		fflush(stderr);	
		return 1;
	}

	chime_app_init(cleanup);

	net_init();

	if (chime_cpu_create(0, 0, cpu_master_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(0, 0, cpu_slave_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(0, 0, cpu_slave_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(1000, -10, cpu_slave_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

#if 0
	if (chime_cpu_create(0, 0, cpu_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(0, -1, cpu_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(0, -1, cpu_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}

	if (chime_cpu_create(0, -1, cpu_reset) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");	
		fflush(stderr);	
	}
#endif	
	chime_sleep(1);
	tracef(T_DBG, "speed set to x10 ...");
	chime_sim_speed_set(10);
	chime_sleep(1);
	chime_reset_all();

#if 0
	chime_sleep(5);
	tracef(T_DBG, "speed set to 10 times...");
	chime_sim_speed_set(10);

	chime_sleep(5);
	tracef(T_DBG, "speed set to 100 times...");
	chime_sim_speed_set(100);

	chime_sleep(5);
	tracef(T_DBG, "speed set to 1000 times...");
	chime_sim_speed_set(1000);

	chime_sleep(5);
	tracef(T_DBG, "speed set to 10000 times...");
	chime_sim_speed_set(10000);
#endif

	for (;;) {
//		printf(".");	
//		fflush(stdout);	
		chime_sleep(5);
	//	if (!chime_reset_all())
	//		break;
	}

	cleanup();

	return 0;
}


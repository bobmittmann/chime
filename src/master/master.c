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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <inttypes.h>


#include "chime.h"
#include "synclk.h"
#include "debug.h"



/****************************************************************************
 * Local Clock
 ****************************************************************************/

//#define CLOCK_FREQ_HZ 200
#define CLOCK_FREQ_HZ 2

static struct clock local_clock;
static volatile bool pps_flag;

void clock_timer_isr(void)
{
	if (clock_tick(&local_clock))
		pps_flag = true;
}


void clock_pps_wait(void) 
{
	pps_flag = false;
	do {
		chime_cpu_wait();
	} while (!pps_flag);
}

/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

void cpu_master(void)
{
	unsigned int period_us;
	struct synclk_pkt pkt;
	int cnt = 0;
	int var;
	
	tracef(T_DBG, "Master reset...");

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL, NULL);

	/* open a simulation variable recorder */
	var = chime_var_open("master_time");

	/* initialize local clock */
	clock_init(&local_clock, CLOCK_FREQ_HZ);


	/* start the clock tick timer */
	period_us = 1000000 / CLOCK_FREQ_HZ;
	chime_tmr_init(0, clock_timer_isr, period_us, period_us);

	pkt.sequence = 0;

	for (;;) {
		clock_pps_wait(); 

		if (++cnt == SYNCLK_POLL) {
			uint64_t local;
			char s[3][64];
	
			local = clock_timestamp(&local_clock);

			//chime_var_rec(var, LFP2D(local) - chime_cpu_time());
			tracef(T_INF, "clk=%s", tsfmt(s[0], local));

			pkt.timestamp = local;
			chime_comm_write(ARCNET_COMM, &pkt, sizeof(pkt));

			cnt = 0;
		}
	}
}

#define ARCNET_SPEED_BPS (2500000 / 4)
#define ARCNET_BIT_TM (1.0 / ARCNET_SPEED_BPS)

int main(int argc, char *argv[]) 
{
	struct comm_attr attr = {
		.wr_cyc_per_byte = 0,
		.wr_cyc_overhead = 0,
		.rd_cyc_per_byte = 0,
		.rd_cyc_overhead = 0,
		.bits_overhead = 6,
		.bits_per_byte = 11,
		.nodes_max = 63,
		.bytes_max = 256,
		.speed_bps = ARCNET_SPEED_BPS, 
		.max_jitter = 12000 * ARCNET_BIT_TM, /* seconds */
		.min_delay = 272 * ARCNET_BIT_TM,  /* minimum delay in seconds */
		.nod_delay = 100 * ARCNET_BIT_TM,  /* per node delay in seconds */
		.hist_en = true, /* enable distribution histogram */
		.txbuf_en = true, /* enable xmit buffer */
		.dcd_en = false, /* enable data carrier detect */
		.exp_en = true/* enable exponential distribution */
	};

	printf("\n==== ARCnet Master ====\n");
	fflush(stdout);

	/* intialize simulation client */
	if (chime_client_start("chronos") < 0) {
		ERR("chime_client_start() failed!");	
		return 1;
	}

	/* intialize application */
	chime_app_init((void (*)(void))chime_client_stop);

	/* create an archnet communication simulation */
	if (chime_comm_create("ARCnet", &attr) < 0) {
		WARN("chime_comm_create() failed!");	
	}

	if (chime_cpu_create(0, -0.5, cpu_master) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	chime_reset_all();

	chime_except_catch(NULL);

	chime_client_stop();

	return 0;
}


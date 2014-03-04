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
#include <inttypes.h>

#include "chime.h"
#include "synclk.h"
#include "debug.h"


/****************************************************************************
 * Local Clock
 ****************************************************************************/

#define CLOCK_FREQ_HZ 200

static struct clock local_clock;

void clock_timer_isr(void)
{
	DBG3("tick");

	if (clock_tick(&local_clock))
		synclk_pps();
}

/****************************************************************************
 * Local RTC chip connection 
 ****************************************************************************/

#define ARCNET_DELAY 0.15
#define REMOTE_PRECISION 0.005

static bool arcnet_pkt_rcvd;

/* This ISR is called when data from the RTC is received on the I2c */
void arcnet_rcv_isr(void)
{
	arcnet_pkt_rcvd = true;;
}

/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

void cpu_slave(void)
{
	unsigned int period_us;
	struct synclk_pkt pkt;
	int var;
	
	tracef(T_DBG, "Slave reset...");
	printf(" - Reset: ID=%02x\n", chime_cpu_id());
	fflush(stdout);

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", arcnet_rcv_isr, NULL, NULL);

	/* open a simulation variable recorder */
	var = chime_var_open("slave_time");

	/* initialize local clock */
	clock_init(&local_clock, CLOCK_FREQ_HZ);

//	clock_offs_adjust(&local_clock, CLK_SECS(-0.050));

	/* initialize clock synchronization */
	synclk_init(&local_clock, ARCNET_DELAY, REMOTE_PRECISION);

	/* start the clock tick timer */
	period_us = 1000000 / CLOCK_FREQ_HZ;
	chime_tmr_init(0, clock_timer_isr, period_us, period_us);

	arcnet_pkt_rcvd = false;
	for (;;) {
		do {
			chime_cpu_wait();
		} while (!arcnet_pkt_rcvd);

		if (arcnet_pkt_rcvd) {
			uint64_t local;
			uint64_t remote;
			int64_t diff;
			char s[3][64];

			/* read from ARCNET*/
			chime_comm_read(ARCNET_COMM, &pkt, sizeof(pkt));
			arcnet_pkt_rcvd = false;

			remote = pkt.timestamp;
			local = clock_timestamp(&local_clock);
			diff = (int64_t)(remote - local);

			tracef(T_INF, "clk=%s offs=%9.6f", 
				   tsfmt(s[0], local), LFP2D(diff));
			//chime_var_rec(var, LFP2D(local) - chime_cpu_time());

			(void)s; (void)diff;
			DBG1("remote=%s local=%s diff=%s", 
				tsfmt(s[0], remote), tsfmt(s[1], local), tsfmt(s[2], diff));

			synclk_receive(remote, local);
		}
	}
}

void dummy_network_node(void)
{
	tracef(T_DBG, "Node reset...");
	printf(" - Network node: ID=%02x\n", chime_cpu_id());
	fflush(stdout);

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL, NULL);

	for (;;) {
		chime_cpu_wait();
	}
}

#define NETWORK_NODES 52

int main(int argc, char *argv[]) 
{
	int i;

	printf("\n==== ARCnet Slave ====\n");
	fflush(stdout);

	/* intialize simulation client */
	if (chime_client_start("chronos") < 0) {
		ERR("chime_client_start() failed!");	
		return 1;
	}

	/* intialize application */
	chime_app_init((void (*)(void))chime_client_stop);

	if (chime_cpu_create(-50, -0.5, cpu_slave) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	for (i = 0; i < NETWORK_NODES; ++i) { 
		if (chime_cpu_create(100, 0, dummy_network_node) < 0) {
			ERR("chime_cpu_create() failed!");	
			chime_client_stop();
			return 4;
		}
	}

	chime_reset_all();

	chime_except_catch(NULL);

	chime_client_stop();

	return 0;
}


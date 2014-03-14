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
 * XXX: Simultaion 
 ****************************************************************************/

static volatile bool pps_flag;
volatile bool rtc_poll_flag; 

void rtc_poll_tmr_isr(void)
{
	rtc_poll_flag = true;
}

int slave_clk_var;
int slave_temp_var;

#define LOCAL_CLOCK_TMR 0

#define ARCNET_COMM 0

#define SIM_POLL_JITTER_US 5000
#define SIM_TEMP_MIN -20
#define SIM_TEMP_MAX 70
#define SIM_TIME_HOURS 6
#define SIM_DUMMY_NETWORK_NODES 0

/****************************************************************************
 * Local Clock
 ****************************************************************************/

#define LOCAL_CLOCK_FREQ_HZ 9

static struct clock local_clock;

/* Clock timeer interrupt handler */
void local_clock_tmr_isr(void)
{
	if (clock_tick(&local_clock))
		pps_flag = true;
}

void local_clock_init(void)
{
	/* initialize the clock structure */
	clock_init(&local_clock, LOCAL_CLOCK_FREQ_HZ, LOCAL_CLOCK_TMR);

	{ /* XXX: simultaion */
		unsigned int period_us;

		/* start the clock tick timer */
		period_us = 1000000 / LOCAL_CLOCK_FREQ_HZ;
		chime_tmr_init(LOCAL_CLOCK_TMR, local_clock_tmr_isr, 
					   period_us, period_us);
	}
}

void local_clock_pps(void)
{
}

/****************************************************************************
 * ARCnet
 ****************************************************************************/

#define REMOTE_PRECISION (FLOAT_CLK(0.005))

static bool arcnet_pkt_rcvd;

/* This ISR is called when data from the RTC is received on the I2c */
void arcnet_rcv_isr(void)
{
	arcnet_pkt_rcvd = true;;
}

/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

struct clock_filt filt;
struct clock_pll pll;

void cpu_slave(void)
{
	struct synclk_pkt pkt;
	uint64_t remote_ts = 0;
	int node_cnt = 0;
	
	tracef(T_DBG, "Slave reset...");
	printf(" - Reset: ID=%02x\n", chime_cpu_id());
	fflush(stdout);

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", arcnet_rcv_isr, NULL, NULL);

	/* open a simulation variable recorder */
	slave_clk_var = chime_var_open("slave_clk");

	/* initialize clock synchronization */
	filt_init(&filt, &local_clock);
	pll_init(&pll, &local_clock);
	
	/* initialize local clock */
	local_clock_init();

	/* XXX: simulation. Set the initial clock offset */
	clock_step(&local_clock, FLOAT_CLK(-0.050));

	arcnet_pkt_rcvd = false;
	for (;;) {
		int n;

		chime_cpu_wait();

		/* PPS .... */
		if (pps_flag) { 
			uint64_t ts;

			pps_flag = false;

			ts = clock_time_get(&local_clock);
			chime_var_rec(slave_clk_var, CLK_DOUBLE(ts) - chime_cpu_time());

			pll_step(&pll);
		}

		n = chime_comm_nodes(ARCNET_COMM);
		if (node_cnt != n) {
			uint32_t arcnet_delay;

			node_cnt = n;
			arcnet_delay = FLOAT_CLK(0.000084 * n + 0.00087);
			/* Reset filter. This function should be called whenever the
			   network reconfigures itself */
			filt_reset(&filt, arcnet_delay, REMOTE_PRECISION);
		}

		if (arcnet_pkt_rcvd) {
			uint64_t local_ts;
			int64_t offs;
			int64_t itvl;

			/* read from ARCNET*/
			chime_comm_read(ARCNET_COMM, &pkt, sizeof(pkt));
			arcnet_pkt_rcvd = false;

			itvl = (int64_t)(pkt.timestamp - remote_ts); 
			remote_ts = pkt.timestamp;
			local_ts = clock_time_get(&local_clock);

			offs = filt_receive(&filt, remote_ts, local_ts);
			tracef(T_INF, "clk=%s offs=%s", FMT_CLK(local_ts), FMT_CLK(offs));

			DBG1("remote=%s local=%s offs=%s", 
				 FMT_CLK(remote_ts), FMT_CLK(local_ts), FMT_CLK(offs));

			pll_phase_adjust(&pll, offs, itvl);
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

	for (i = 0; i < SIM_DUMMY_NETWORK_NODES; ++i) { 
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


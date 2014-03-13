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
#include "rtc-sim.h"

/****************************************************************************
 * XXX: Simultaion 
 ****************************************************************************/

static volatile bool pps_flag;
volatile bool rtc_poll_flag; 

void rtc_poll_tmr_isr(void)
{
	rtc_poll_flag = true;
}

int rtc_clk_var;
int rtc_time_var;
int master_clk_var;
int master_temp_var;

#define LOCAL_CLOCK_TMR 0
#define RTC_POLL_TMR 1

#define ARCNET_COMM 0
#define I2C_RTC_COMM 1

#define SIM_POLL_JITTER_US 5000
#define SIM_TEMP_MIN -20
#define SIM_TEMP_MAX 70

#define SIM_TIME_HOURS 2

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

/****************************************************************************
 * RTC
 ****************************************************************************/

#define RTC_POLL_FREQ_HZ 8
#define RTC_POLL_SHIFT_PPM 250
#define FLL_ERR_MAX FLOAT_CLK(2.0 / RTC_POLL_FREQ_HZ)

struct rtc_clock {
	uint64_t ts;
	int64_t period;
	int8_t sec;
};

struct rtc_clock rtc_clk;

struct clock_fll fll;

#define RTC_OFFS_MAX FLOAT_CLK(2.0 / RTC_POLL_FREQ_HZ)

/* This function simulates the polling of the RTC. 
   It should be called at RTC_POLL_FREQ_HZ frequency. */
void rtc_poll(void)
{
	struct rtc_tm rtm; /* time structure from the RTC chip */
	uint64_t rtc_ts;
	int64_t offs;

	/* read from the RTC Chip */
	rtc_read(&rtm);

	/* update the RTC clock estimated time */
	rtc_clk.ts += rtc_clk.period;

	/* wait for next RTC second count */
	if (rtc_clk.sec == rtm.seconds)
		return;

	/* save the seconds */
	rtc_clk.sec = rtm.seconds;

	/* Convert the RTC time structure into clock timestamp */
	rtc_ts = rtc_timestamp(&rtm);
	
	DBG1("rtc=%s clk=%s", FMT_CLK(rtc_ts), FMT_CLK(rtc_clk.ts));

	offs = (int64_t)(rtc_ts - rtc_clk.ts);

	if ((offs >= RTC_OFFS_MAX) || (offs <= -RTC_OFFS_MAX)) {
		/* if we are off by too much step the clock */

		DBG("clk=%s offs=%s !STEP!", FMT_CLK(rtc_ts), 
			FMT_CLK(offs));

		DBG5("STEP clk=%"PRIu64" offs=%"PRId64" max=%"PRId64, 
			 rtc_ts, offs, RTC_OFFS_MAX);

		/* Force RTC clock time to the hardware value */
		rtc_clk.ts = rtc_ts;

		/* reset the FLL */
		fll_reset(&fll, rtc_clk.ts);
	} else { 
		DBG2("clk=%s offs=%s", FMT_CLK(rtc_ts), FMT_CLK(offs));
		rtc_clk.ts += offs;

		fll_step(&fll, rtc_clk.ts, offs);
	}

	chime_var_rec(rtc_clk_var, CLK_DOUBLE(rtc_clk.ts) - chime_cpu_time());

}

void rtc_clock_init(void)
{
	/* Initialize the seconds count with an invalid value.
	   This will force a seconds update on the first round. */
	rtc_clk.sec = -1; 
	rtc_clk.ts = 0;
	rtc_clk.period = FLOAT_CLK(1.0 / RTC_POLL_FREQ_HZ);

	/* Initialize the FLL.
	   Connect the local clock to the FLL discipline. */
	fll_init(&fll, &local_clock, FLL_ERR_MAX);


	{ /* XXX: simulation only!! */
		unsigned int itv_us;

		rtc_connect(I2C_RTC_COMM);

		rtc_poll_flag = false;
		/* WARNING: 
		   RTC_SHIFT_PPM is used to force the timer poll roling..
		   This is necessary to detect edges, allowing the FLL to converge.
		 */
		itv_us = (1000000 + RTC_POLL_SHIFT_PPM) / RTC_POLL_FREQ_HZ;
		chime_tmr_init(RTC_POLL_TMR, rtc_poll_tmr_isr, itv_us, itv_us);

		rtc_clk_var = chime_var_open("rtc_clk");
		rtc_time_var = chime_var_open("rtc_time");
		//	rtc_syn_var = chime_var_open("rtc_syn");
	}
}

/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/
static __thread int sim_minutes = SIM_TIME_HOURS * 60;
static __thread float sim_temperature;
static __thread float sim_temp_rate;

void sim_timer_isr(void)
{
	if (--sim_minutes == 0) {
		tracef(T_DBG, "Halting the simulation.");
		chime_sim_vars_dump();
		chime_cpu_halt();
//		chime_cpu_self_destroy();
	}

	DBG1("temp=%.2f dg.C clk=%.1f ppm", 
		 chime_cpu_temp_get(), chime_cpu_ppm_get());

	if (sim_temperature >= SIM_TEMP_MAX) {
		sim_temp_rate *= -1.5;
	} else if (sim_temp_rate < 0) {
		if (sim_temperature <= 25)
			sim_temp_rate = 0;
	}

	sim_temperature += sim_temp_rate;
	chime_cpu_temp_set(sim_temperature);

	chime_var_rec(master_temp_var, ((sim_temperature) / 100) + 1);

	if ((sim_minutes % 15) == 0) {
		chime_sim_vars_dump();
	}
}

void cpu_master(void)
{
	struct synclk_pkt pkt;
	uint64_t local;
	int cnt = 0;

	(void)cnt;
	(void)pkt;

	sim_temperature = SIM_TEMP_MIN;
	chime_cpu_temp_set(sim_temperature);
	sim_temp_rate = (2.0 * SIM_TEMP_MAX - SIM_TEMP_MIN) / sim_minutes;
	tracef(T_DBG, "Master temperature rate = %.3f dg/minute.", sim_temp_rate);

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL, NULL);

	/* open simulation variable recorders */
	master_temp_var = chime_var_open("master_temp");
	master_clk_var = chime_var_open("master_clk");

	/* set an 1 minute interval timer for simulation  */
	chime_tmr_init(3, sim_timer_isr, 60000000, 60000000);

	/* initialize RTC clock */
	rtc_clock_init();

	/* initialize local clock */
	local_clock_init();

	pkt.sequence = 0;

	for (;;) {
		chime_cpu_wait();

		/* PPS .... */
		if (pps_flag) { 
			pps_flag = false;

			local = clock_time_get(&local_clock);
			chime_var_rec(master_clk_var, CLK_DOUBLE(local) - 
						  chime_cpu_time());

			if (++cnt == SYNCLK_POLL) {

				//chime_var_rec(var, LFP2D(local) - chime_cpu_time());
				tracef(T_INF, "clk=%s", FMT_CLK(local));

				pkt.timestamp = local;
				chime_comm_write(ARCNET_COMM, &pkt, sizeof(pkt));

				cnt = 0;
			}
		}

		/* RTC polling .... */
		if (rtc_poll_flag) {
			rtc_poll_flag = false;

			/* simulate a random delay */
			chime_cpu_step(rand() % SIM_POLL_JITTER_US);
			rtc_poll();
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

	/* Commonn value for Tc = 0.04 ppm,
		Limits: 
		- offset +-200 ppm  
		- temp drift: -0.1 ppm  
		Bad crystal: 
		- offset +-100 ppm  
		- temp drift: -0.05 ppm  
		Good crystal: 
		- offset +-25 ppm  
		- temp drift: -0.025 ppm  
	 */
	if (chime_cpu_create(-200, -0.10, cpu_master) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	rtc_sim_init();

//	chime_reset_all();

	chime_except_catch(NULL);

	chime_client_stop();

	return 0;
}


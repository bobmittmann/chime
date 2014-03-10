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

#define ARCNET_COMM 0
#define I2C_RTC_COMM 1

/****************************************************************************
 * Local Clock
 ****************************************************************************/

#define LOCAL_CLOCK_FREQ_HZ 200
#define LOCAL_CLOCK_TMR 0

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

#define CPU_FREQ_HZ 1000000

uint64_t local_clock_timestamp(void) 
{
	uint64_t ts = clock_timestamp(&local_clock);
	uint32_t cnt = chime_tmr_count(LOCAL_CLOCK_TMR);
	float dt;

	dt = ((1.0 * LOCAL_CLOCK_FREQ_HZ) / CPU_FREQ_HZ) * 
		cnt * local_clock.increment;

	return ts + dt;
}


/****************************************************************************
 * RTC
 ****************************************************************************/

#define RTC_POLL_FREQ_HZ 8

static struct clock local_clock;
static volatile bool pps_flag;

struct rtc_clock {
	uint8_t sec;
	bool rx_done;
	uint64_t ts;
	int64_t dt;
	int32_t e;

	struct {
		uint32_t cnt;
		uint32_t secs;
		uint64_t ts;
		int32_t drift;
		int32_t drift_comp;
	} fll;
};

struct rtc_clock rtc;

int rtc_clk_var;
int rtc_syn_var;

#define RTC_OFFS_MAX D2DT(2.0 / RTC_POLL_FREQ_HZ)
#define RTC_FLL_PERIOD  (20000 / RTC_POLL_FREQ_HZ)

void rtc_poll_timer_isr(void)
{
	uint64_t ts;
	char s[3][64];
	struct rtc_tm rtm;
	int32_t e;

	(void)s;

	e = rtc.e / 64;
	rtc.e -= e;
	rtc.ts += rtc.dt + e;
	
	rtc_read(&rtm);

	/* pool the RTC, waiting for next second count */
	if (rtc.sec != rtm.seconds) {
		int64_t offs;

	//	ts = rtc_timestamp(&rtm);
		ts = rtc_timestamp(&rtm) + D2DT(0.5 / RTC_POLL_FREQ_HZ);

		rtc.sec = rtm.seconds;


		offs = (int64_t)(ts - rtc.ts);

		if ((offs >= RTC_OFFS_MAX) || (offs <= -RTC_OFFS_MAX)) {
			rtc.e = 0;
			/* force RTC clock time */
			rtc.ts = ts;
			DBG("clk=%s offs=%.4f !STEP!", tsfmt(s[0], ts), TS2D(offs));
			DBG5("STEP clk=%"PRIu64" offs=%"PRId64" max=%"PRId64, 
				ts, offs, RTC_OFFS_MAX);

			/* set local clock */
//			clock_step(&local_clock, ts);
			/* reset fll */
			rtc.fll.secs = 0;
			rtc.fll.cnt = 0;
			rtc.fll.ts = clock_timestamp(&local_clock);
		} else {
			DBG1("clk=%s offs=%.4f", tsfmt(s[0], ts), TS2D(offs));
			rtc.e = (int32_t)offs;

			/* local clock drift compenastion */
			if (++rtc.fll.secs == (1 << rtc.fll.cnt) * RTC_FLL_PERIOD) {
				int64_t dt;
				int32_t drift;
				float freq;

				/* get local clock timestamps */
				dt = clock_timestamp(&local_clock) - rtc.fll.ts;
				rtc.fll.ts += dt;

				freq = DT2FLOAT(dt) / rtc.fll.secs;

		//		drift = (2 * rtc.fll.drift + Q31(1.0 - freq)) / 2;
				drift = rtc.fll.drift + Q31(1.0 - freq);

				rtc.fll.drift = clock_drift_adjust(&local_clock, drift);

				INF("FLL dt=%d freq=%.8f drift=%.8f.", rtc.fll.secs,
					freq, FRAC2FLOAT(drift));

				rtc.fll.secs = 0;
				if (rtc.fll.cnt < 0)
					rtc.fll.cnt++;
			} else {
				clock_phase_adjust(&local_clock, (int32_t)offs, 0);
			}
		}
	
		chime_var_rec(rtc_clk_var, TS2D(ts) - chime_cpu_time());
		chime_var_rec(rtc_syn_var, TS2D(rtc.ts) - chime_cpu_time());
	}
}


void rtc_clock_init(void)
{
	int period_us; 

	rtc_connect(I2C_RTC_COMM);

	rtc.ts = 0;
	rtc.e = 0;
	rtc.dt = D2TS(1.0 / RTC_POLL_FREQ_HZ);

	period_us = 1000000 / RTC_POLL_FREQ_HZ;
	chime_tmr_init(1, rtc_poll_timer_isr, period_us, period_us);

	rtc_clk_var = chime_var_open("rtc_clk");
	rtc_syn_var = chime_var_open("rtc_syn");


	rtc.fll.secs = 0;
	rtc.fll.cnt = 0;
	rtc.fll.drift = 0;
	rtc.fll.ts = clock_timestamp(&local_clock);
}


/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

static __thread int abort_minutes = 180;

void abort_timer_isr(void)
{

	if (--abort_minutes == 0) {
		chime_sim_vars_dump();
		chime_cpu_self_destroy();
	}
}

void cpu_master(void)
{
	unsigned int period_us;
	struct synclk_pkt pkt;
	int cnt = 0;
	int var;

	(void)cnt;
	(void)pkt;
	(void)period_us;
	

	tracef(T_DBG, "Master reset...");

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL, NULL);

	/* open a simulation variable recorder */
	var = chime_var_open("master_clk");
	(void)var;

	/* initialize local clock */
	clock_init(&local_clock, LOCAL_CLOCK_FREQ_HZ);

	/* initialize RTC clock */
	rtc_clock_init();

	/* start the clock tick timer */
	period_us = 1000000 / LOCAL_CLOCK_FREQ_HZ;
	chime_tmr_init(LOCAL_CLOCK_TMR, clock_timer_isr, period_us, period_us);
	chime_tmr_init(3, abort_timer_isr, 60000000, 60000000);

	pkt.sequence = 0;

	for (;;) {
		uint64_t local;
		clock_pps_wait(); 

		local = clock_timestamp(&local_clock);
		chime_var_rec(var, TS2D(local) - chime_cpu_time());

#if 0

		if (++cnt == SYNCLK_POLL) {
			char s[3][64];
	
			local = clock_timestamp(&local_clock);

			//chime_var_rec(var, LFP2D(local) - chime_cpu_time());
			tracef(T_INF, "clk=%s", tsfmt(s[0], local));

			pkt.timestamp = local;
			chime_comm_write(ARCNET_COMM, &pkt, sizeof(pkt));

			cnt = 0;
		}
#endif
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

	if (chime_cpu_create(+233, -0.5, cpu_master) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	rtc_sim_init();

	chime_reset_all();

	chime_except_catch(NULL);

	chime_client_stop();

	return 0;
}


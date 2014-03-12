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

#define LOCAL_CLOCK_TMR 0
#define RTC_POLL_TMR 1
#define POLL_DELAY_MAX_US 1

#define ARCNET_COMM 0
#define I2C_RTC_COMM 1


/****************************************************************************
 * Local Clock
 ****************************************************************************/

#define LOCAL_CLOCK_FREQ_HZ 200

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
	clock_init(&local_clock, LOCAL_CLOCK_FREQ_HZ);

	{ /* XXX: simultaion */
		unsigned int period_us;

		/* start the clock tick timer */
		period_us = 1000000 / LOCAL_CLOCK_FREQ_HZ;
		chime_tmr_init(LOCAL_CLOCK_TMR, local_clock_tmr_isr, 
					   period_us, period_us);
	}
}

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) 
 ****************************************************************************/

void fll_reset(struct clock_fll  * fll)
{
	fll->run = false;
	fll->lock = false;
	fll->drift = 0;
	fll->err = 0;
}

/****************************************************************************
 * RTC
 ****************************************************************************/

#define RTC_POLL_FREQ_HZ 8

struct rtc_clock {
	uint64_t ts;
	int64_t period;
	int8_t sec;
	int32_t edge_offs;
	uint32_t edge_filt;
};

struct rtc_clock rtc;

struct clock_fll fll;

#define RTC_OFFS_MAX FLOAT_CLK(2.0 / RTC_POLL_FREQ_HZ)
#define FLL_OFFS_MAX FLOAT_CLK(1.0 / RTC_POLL_FREQ_HZ)
#define RTC_FLL_PERIOD  (10000 / RTC_POLL_FREQ_HZ)


/* This function simulates the polling of the RTC. 
   It should be called at RTC_POLL_FREQ_HZ frequency. */
void rtc_poll(void)
{
	uint64_t ts;
	char s[3][64];
	/* time structure from the RTC chip */
	struct rtc_tm rtm;
	int64_t offs;
	(void)s;

	/* read from the RTC Chip */
	rtc_read(&rtm);

	/* update the RTC clock */
	rtc.ts += rtc.period;

	/* pool the RTC, waiting for next second count */
	if (rtc.sec == rtm.seconds)
		return;

	/* Convert the RTC time structure into clock timestamp.
	   Add half of the rtc swing. This will make the reading
	   of the RTC to oscillate centered int the hardware RTC time... */
	ts = rtc_timestamp(&rtm) + FLOAT_CLK(0.5 / RTC_POLL_FREQ_HZ);
//	ts = rtc_timestamp(&rtm);
	
	DBG1("rtc=%s clk=%s", fmt_clk(s[0], ts), fmt_clk(s[1], rtc.ts));

	rtc.sec = rtm.seconds;

	offs = (int64_t)(ts - rtc.ts);

	if ((offs >= RTC_OFFS_MAX) || (offs <= -RTC_OFFS_MAX)) {
		/* if we are off by too much step the clock */

		/* force RTC clock time */
		rtc.ts = ts;
		rtc.edge_filt = 0;

		DBG1("clk=%s offs=%s !STEP!", fmt_clk_ms(s[0], ts), 
			fmt_clk_ms(s[1], offs));

		DBG5("STEP clk=%"PRIu64" offs=%"PRId64" max=%"PRId64, 
			 ts, offs, RTC_OFFS_MAX);

		/* set local clock */
		clock_time_set(&local_clock, ts);

		/* reset the FLL */
		fll_reset(&fll);
	} else {
		DBG2("clk=%s offs=%s", fmt_clk_ms(s[0], ts), 
			fmt_clk_ms(s[1], offs));
		rtc.ts += offs;

/*		if ((rtc.edge_offs) && (offs)) {
			if (rtc.edge_offs != offs) {
				ERR("edge=%s offs=%s ignoring", 
					fmt_clk(s[0], rtc.edge_offs), 
					fmt_clk(s[1], offs));
				offs = 0;
			}
		} */
		if (rtc.edge_filt) {
			offs = 0;
			rtc.edge_filt--;
		} else  if (offs != 0) {
			DBG1("clk=%s offs=%s", fmt_clk(s[0], ts), fmt_clk(s[1], offs));
			/* Enable edge filter window to avoid detecting 
			   a second transition right after the first one. */
//			rtc.edge_filt = 900; /* edge filter window (seconds) */
		}

		if (!fll.run) {
			int64_t dt;

			dt = rtc.ts - clock_time_get(&local_clock);
			fll.err = dt;

			DBG1("dt=%s offs=%s", fmt_clk(s[0], dt), fmt_clk(s[1], offs));

			if (offs != 0) {
				/* capture the NOW timetamps */
				fll.rtc_ts = rtc.ts;
				/* get raw local clock (no offset) */
				fll.clk_ts = clock_timestamp(&local_clock);
				fll.run = true;
				fll.lock = false;
				DBG("FLL started: %.3f!", CLK_FLOAT(fll.rtc_ts));
				rtc.edge_offs = offs;
				/* Enable edge filter window to avoid detecting 
				   a second transition right after the first one. */
				rtc.edge_filt = 900; /* edge filter window (seconds) */
			}
		} else {

//			if (offs && (rtc.edge_offs == offs)) {
			if (offs) {
				int64_t clk_dt;
				int64_t rtc_dt;
				int32_t drift;
				float freq;
				float dy;
				float dx;

				rtc.edge_offs = offs;
				/* Enable edge filter window to avoid detecting 
				   a second transition right after the first one. */
				rtc.edge_filt = 900; /* edge filter window (seconds) */

				if (!fll.lock) {
					int64_t dt;

					dt = rtc.ts - clock_time_get(&local_clock);
					fll.err = dt - (offs / 2);
					fll.lock = true;
				} else {
					int64_t dt;

					dt = rtc.ts - clock_time_get(&local_clock);
					fll.err = dt - (offs / 2);
				}

				/* get raw local clock (no offset) */
				clk_dt = clock_timestamp(&local_clock) - fll.clk_ts;
				/* update the timetamp for next round */
				fll.clk_ts += clk_dt;

				rtc_dt = rtc.ts - fll.rtc_ts;
				fll.rtc_ts += rtc_dt;

				dy = CLK_FLOAT(clk_dt);
				dx = CLK_FLOAT(rtc_dt);

				freq = dy / dx;

				DBG1("FLL dy=%.6f dx=%.6f", dy, dx);

				drift = fll.drift + FLOAT_Q31(1.0 - freq);
				fll.drift = clock_drift_comp(&local_clock, drift);

				INF("FLL freq=%.9f drift=%.9f", freq, Q31_FLOAT(fll.drift));
			}
		}
	}

	/* exponential amortization for the local clock error */
	if (fll.err) {
		int32_t e;

		e = fll.err / 64;
		if (e) {
			fll.err -= e;
			clock_step(&local_clock, e);
		}
	}


	chime_var_rec(rtc_clk_var, CLK_DOUBLE(rtc.ts) - chime_cpu_time());
}

void rtc_clock_init(void)
{
	/* Initialize the second count with an invalid value.
	   This will force the second update on the first round. */
	rtc.sec = -1; 
	rtc.ts = 0;
	rtc.edge_offs = 0;
	rtc.edge_filt = 0;
	rtc.period = FLOAT_CLK(1.0 / RTC_POLL_FREQ_HZ);

	/* Initialize the FLL */
	fll_reset(&fll);

	{ /* XXX: simulation!! */
		unsigned int itv_us;

		rtc_connect(I2C_RTC_COMM);

		rtc_poll_flag = false;
		itv_us = 1000000 / RTC_POLL_FREQ_HZ;
		chime_tmr_init(RTC_POLL_TMR, rtc_poll_tmr_isr, itv_us, itv_us);

		rtc_clk_var = chime_var_open("rtc_clk");
		//	rtc_syn_var = chime_var_open("rtc_syn");
	}
}


/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

static __thread int sim_minutes = 12 * 60;
static __thread float temperature = -20;
static __thread float temp_rate = 0.1;

void abort_timer_isr(void)
{
	if (--sim_minutes == 0) {
		chime_sim_vars_dump();
		chime_cpu_self_destroy();
	}

	DBG("clk offset: %.1f ppm", chime_cpu_ppm_get()); 
	chime_cpu_temp_set(temperature += temp_rate);

	if ((sim_minutes % 30) == 0) {
		chime_sim_vars_dump();
	}
}

void cpu_master(void)
{
	struct synclk_pkt pkt;
	int cnt = 0;
	int var;

	(void)cnt;
	(void)pkt;

	tracef(T_DBG, "Master reset...");
	chime_cpu_temp_set(temperature);
	temp_rate = (70 - temperature) / sim_minutes;

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL, NULL);

	/* open a simulation variable recorder */
	var = chime_var_open("master_clk");
	(void)var;

	/* initialize RTC clock */
	rtc_clock_init();

	/* initialize local clock */
	local_clock_init();

	chime_tmr_init(3, abort_timer_isr, 60000000, 60000000);

	pkt.sequence = 0;

	for (;;) {
		uint64_t local;

		for (;;) { /* XXX: simulation */
			chime_cpu_wait();

			if (pps_flag) {
				pps_flag = false;
				break;
			}	

			if (rtc_poll_flag) {
				rtc_poll_flag = false;

				/* simulate a random delay */
				chime_cpu_step(rand() % POLL_DELAY_MAX_US);
				rtc_poll();
			}	
		}

		local = clock_time_get(&local_clock);
		chime_var_rec(var, CLK_DOUBLE(local) - chime_cpu_time());

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
	/* A common */
	if (chime_cpu_create(-0, -0.025, cpu_master) < 0) {
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


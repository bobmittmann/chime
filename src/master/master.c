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
#define TIMER_FREQ_HZ 1000000

static struct clock local_clock;

bool local_clock_tick(void)
{
	register uint64_t ts;
	register uint32_t sec;

	ts = local_clock.timestamp;
	sec = ts >> 32;
	local_clock.timestamp = ts += local_clock.increment;

	if ((ts >> 32) != sec) {
		/* second update, signal the pps flag */
		return true;
	}

	return false;
}

uint64_t _local_clock_timestamp(void) 
{
	register uint32_t cnt0;
	register uint32_t cnt1;
	register uint64_t ts;
	register float dt;

	do {
		ts = local_clock.timestamp;
		cnt0 = chime_tmr_count(LOCAL_CLOCK_TMR);
		cnt1 = chime_tmr_count(LOCAL_CLOCK_TMR);
	} while (cnt1 < cnt0);

	dt = ((1.0 * LOCAL_CLOCK_FREQ_HZ) / TIMER_FREQ_HZ) * 
		cnt1 * local_clock.increment;

	if (dt != 0) {
		DBG("cnt0=%d cnt1=%d dt=%.8f", cnt0, cnt1, dt);
	}

	return ts + dt;
}

uint64_t local_clock_timestamp(void) 
{
	register uint64_t ts;

	ts = local_clock.timestamp;

	return ts;
}

uint64_t local_clock_time_get(void)
{
	uint64_t ts;
	ts = local_clock_timestamp() + local_clock.offset;
	return ts;
}

void local_clock_time_set(uint64_t ts)
{
	local_clock.offset = (int64_t)(ts - local_clock_timestamp());
}

void local_clock_step(int64_t dt)
{
	local_clock.offset += dt;
}

#define CLOCK_DRIFT_MAX Q31(0.01000)

int32_t local_clock_drift_comp(int32_t drift)
{
	struct clock * clk = &local_clock;
	int32_t d;

	/* limit the maximum drift correction */
	if (drift > CLOCK_DRIFT_MAX)
		drift = CLOCK_DRIFT_MAX;
	else if (drift < -CLOCK_DRIFT_MAX)
		drift = -CLOCK_DRIFT_MAX;

	/* calculate the drift compenastion per tick */
	clk->drift_comp = Q31MUL(drift, CLK_Q31(clk->resolution));
	d = + Q31_CLK(clk->drift_comp);

	/* calculate the new increpent per tick */
	clk->increment = clk->resolution + Q31_CLK(clk->drift_comp);

	INF("FLL d=%d res=%.9f inc=%.9f", d, 
		CLK_FLOAT(clk->resolution), CLK_FLOAT(clk->increment));

	/* return the corrected drift adjustment */
	return clk->drift_comp * LOCAL_CLOCK_FREQ_HZ;
}

static volatile bool pps_flag;

void local_clock_tmr_isr(void)
{
	if (local_clock_tick())
		pps_flag = true;
}


void local_clock_init(void)
{
	unsigned int period_us;

	local_clock.timestamp = 0;
	local_clock.resolution = FLOAT_CLK(1.0 / LOCAL_CLOCK_FREQ_HZ);
	local_clock.frequency = LOCAL_CLOCK_FREQ_HZ;
	local_clock.drift_comp = 0;
	local_clock.increment = local_clock.resolution;
	/* Wed, 01 Jan 2014 00:00:00 GMT */
//	local_clock.offset = (uint64_t)1388534400LL << 32;  

	period_us = 1000000 / LOCAL_CLOCK_FREQ_HZ;
	INF("period=%d.%03d ms, resolution=%.8f", 
		period_us / 1000, period_us % 1000, 
		FRAC2FLOAT(local_clock.resolution));

	/* start the clock tick timer */
	chime_tmr_init(LOCAL_CLOCK_TMR, local_clock_tmr_isr, period_us, period_us);
}

void clock_pps_wait(void) 
{
	pps_flag = false;
	do {
		chime_cpu_wait();
	} while (!pps_flag);
}



/****************************************************************************
 * RTC
 ****************************************************************************/

#define RTC_POLL_FREQ_HZ 8

struct rtc_clock {
	uint8_t sec;
	bool rx_done;
	uint64_t ts;
	int64_t dt;
	int32_t e;

	int32_t drift;

	struct {
		uint32_t cnt;
		uint32_t secs;
		uint64_t rtc_ts;
		uint64_t clk_ts;
		int32_t e;
		bool lock;
		bool run;
	} fll;

	struct clock_pll  pll;
};

struct rtc_clock rtc;

int rtc_clk_var;
//int rtc_syn_var;

#define RTC_OFFS_MAX FLOAT_CLK(2.0 / RTC_POLL_FREQ_HZ)
#define FLL_OFFS_MAX FLOAT_CLK(1.0 / RTC_POLL_FREQ_HZ)
#define RTC_FLL_PERIOD  (10000 / RTC_POLL_FREQ_HZ)

#define POLL_ERR_MAX_US 200
#define RTC_POLL_TMR 1

static __thread struct {
	int32_t itv_us;
	int32_t err;
} rtc_pool;

void rtc_poll_tmr_isr(void)
{
	uint64_t ts;
	char s[3][64];
	struct rtc_tm rtm;
	int64_t offs;
	(void)s;

	{ 
		/* simulate a random polling delay */
		int32_t itv_us;

		rtc_pool.err = (rand() % POLL_ERR_MAX_US) - (POLL_ERR_MAX_US / 2);
		itv_us = rtc_pool.itv_us + rtc_pool.err;
		chime_tmr_reset(RTC_POLL_TMR, itv_us, 0);
	}


	rtc.ts += rtc.dt;
	rtc_read(&rtm);

	/* pool the RTC, waiting for next second count */
	if (rtc.sec == rtm.seconds)
		return;

	if (rtc.fll.e) {
		int32_t e;

		e = rtc.fll.e / 128;
		rtc.fll.e -= e;
		local_clock_step(e);
	}

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

		/* reset FLL */
		rtc.fll.run = false;
		rtc.fll.lock = false;
		/* set local clock */
		local_clock_time_set(ts);
	} else {
		DBG4("clk=%s offs=%.4f", tsfmt(s[0], ts), TS2D(offs));
		rtc.e = (int32_t)offs;
		rtc.ts += offs;

		if (!rtc.fll.run) {
			int64_t dt;

			dt = rtc.ts - local_clock_time_get();
			rtc.fll.e = dt;

			if (offs != 0) {
				rtc.fll.secs = 0;
				rtc.fll.cnt = 0;
				rtc.fll.rtc_ts = rtc.ts;
				rtc.fll.clk_ts = local_clock_timestamp();
				rtc.fll.run = true;
				rtc.fll.lock = false;
				DBG("FLL started: %.3f!", CLK_FLOAT(rtc.fll.rtc_ts));
			}
		} else {
			if (offs != 0) {
				int64_t clk_dt;
				int64_t rtc_dt;
				int32_t drift;
				float freq;
				float dy;
				float dx;

				if (!rtc.fll.lock) {
					int64_t dt;

					dt = rtc.ts - local_clock_time_get();
					rtc.fll.e = dt / 2;
				}

				clk_dt = local_clock_timestamp() - rtc.fll.clk_ts;
				/* update the timetamp for next round */
				rtc.fll.clk_ts += clk_dt;

				rtc_dt = rtc.ts - rtc.fll.rtc_ts;
				rtc.fll.rtc_ts += rtc_dt;

				dy = CLK_FLOAT(clk_dt);
				dx = CLK_FLOAT(rtc_dt);

				freq = dy / dx;

				DBG("FLL dy=%.6f dx=%.6f", dy, dx);

				drift = rtc.drift + FLOAT_Q31(1.0 - freq);
				rtc.drift = local_clock_drift_comp(drift);

				INF("FLL freq=%.9f drift=%.9f", freq, Q31_FLOAT(drift) );
				rtc.fll.lock = true;
			}
		}


#if 0
		/* local clock drift compenastion */
		//			if (++rtc.fll.secs == RTC_FLL_PERIOD) {
		if (++rtc.fll.secs == (1 << rtc.fll.cnt) * RTC_FLL_PERIOD) {
			int64_t dt;
			float freq;
			float dy;
			float dx;
			int32_t drift;

			/* get local clock timestamps */
			dt = local_clock_timestamp() - rtc.fll.ts;
			/* update the timetamp for next round */
			rtc.fll.ts += dt;

			dy = CLK_FLOAT(dt);
			dx = (1 << rtc.fll.cnt) * RTC_FLL_PERIOD;
			freq = dy * (1.0 / dx);

			DBG4("FLL dy=%0.3f dx=%.f", dy, dx);

			//		drift = (2 * rtc.fll.drift + Q31(1.0 - freq)) / 2;
			drift = rtc.drift + FLOAT_Q31(1.0 - freq);

			rtc.drift = local_clock_drift_comp(drift);

			INF("FLL dt=%.1f freq=%.8f drift=%.8f", 
				dx, freq, Q31_FLOAT(drift) );

			rtc.fll.secs = 0;
			if (rtc.fll.cnt < 4)
				rtc.fll.cnt++;

			rtc.fll.lock = true;
		}

		if (!rtc.fll.lock) {
			int64_t dt;

			dt = rtc.ts - local_clock_time_get();
			local_clock_step(dt);
		} else {
			int64_t dt;
			dt = rtc.ts - local_clock_time_get();

			if ((dt > FLL_OFFS_MAX) || (dt < -FLL_OFFS_MAX)) {
				//		rtc.fll.lock = false;
				//		local_clock_step(dt);
			}
		}
#endif
	}

	chime_var_rec(rtc_clk_var, TS2D(rtc.ts) - chime_cpu_time());
//	chime_var_rec(rtc_syn_var, TS2D(rtc.ts) - chime_cpu_time());
}

void __rtc_pps(void)
{
	uint64_t ts;
	int32_t drift;
	int64_t offs;

	ts = local_clock_time_get();
	offs = (int64_t)(rtc.ts - ts);

	if ((offs >= RTC_OFFS_MAX) || (offs <= -RTC_OFFS_MAX)) {
		local_clock_step(rtc.ts);
		pll_reset(&rtc.pll);
		/* FLL reset */
		rtc.fll.secs = 0;
		rtc.fll.cnt = 0;
		rtc.fll.clk_ts = ts;
		return;
	}

	if (1) {
		drift = rtc.drift + pll_pps_step(&rtc.pll, (int32_t)offs);
		rtc.drift = clock_drift_adjust(&local_clock, drift);

		INF("PLL offs=%.8f drift=%.8f.", 
			DT2FLOAT(offs), FRAC2FLOAT(drift));
	} else {
		/* local clock drift compenastion */
//		if (++rtc.fll.secs == (1 << rtc.fll.cnt) * RTC_FLL_PERIOD) {
		if (++rtc.fll.secs == RTC_FLL_PERIOD) {
			int64_t dt;
			float freq;

			/* get local clock timestamps */
			dt = clock_timestamp(&local_clock) - rtc.fll.clk_ts;
			rtc.fll.clk_ts += dt;

			freq = DT2FLOAT(dt) / rtc.fll.secs;

			//		drift = (2 * rtc.fll.drift + Q31(1.0 - freq)) / 2;
			drift = rtc.drift + Q31(1.0 - freq);

			rtc.drift = clock_drift_adjust(&local_clock, drift);

			INF("FLL dt=%d freq=%.8f drift=%.8f.", rtc.fll.secs,
				freq, FRAC2FLOAT(rtc.drift));

			rtc.fll.secs = 0;
			if (rtc.fll.cnt < 0)
				rtc.fll.cnt++;
		
			rtc.fll.lock = true;;
		}
	}
}


void rtc_clock_init(void)
{
	rtc_connect(I2C_RTC_COMM);

	rtc.ts = 0;
	rtc.e = 0;
	rtc.dt = D2TS(1.0 / RTC_POLL_FREQ_HZ);

	rtc_pool.itv_us = 1000000 / RTC_POLL_FREQ_HZ;
	rtc_pool.err = 0;
	chime_tmr_init(RTC_POLL_TMR, rtc_poll_tmr_isr, rtc_pool.itv_us, 0);

	rtc_clk_var = chime_var_open("rtc_clk");
//	rtc_syn_var = chime_var_open("rtc_syn");

	rtc.drift = 0;

	rtc.fll.secs = 0;
	rtc.fll.cnt = 0;
	rtc.fll.clk_ts = clock_timestamp(&local_clock);

	pll_reset(&rtc.pll);
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

	if ((abort_minutes % 10) == 0)
		chime_sim_vars_dump();
}

void cpu_master(void)
{
	struct synclk_pkt pkt;
	int cnt = 0;
	int var;

	(void)cnt;
	(void)pkt;

	tracef(T_DBG, "Master reset...");

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
		clock_pps_wait(); 

		local = local_clock_time_get();
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

	if (chime_cpu_create(-105.3, -0.5, cpu_master) < 0) {
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


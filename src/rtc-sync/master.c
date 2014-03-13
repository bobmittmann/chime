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
 * Local Clock
 ****************************************************************************/

#define CLOCK_FREQ_HZ 200
#define RTC_POOL_FREQ_HZ 8

/****************************************************************************
 * Local RTC chip connection 
 ****************************************************************************/

#define RTC_DELAY 0.0001
#define RTC_PRECISION 0.001

static bool rtc_rx_done;

/* This ISR is called when data from the RTC is received on the I2c */
void i2c_rcv_isr(void)
{
	DBG3("rtc_rx_done!");
	rtc_rx_done = true;;
}

void rtc_clock_read(struct rtc_tm * rtm)
{
	uint8_t pkt[2];

	/* request I2C read */
	pkt[0] = 0x01; 
	chime_comm_write(I2C_RTC_COMM, pkt, 1);
	/* wait for I2C interrupt */
	rtc_rx_done = false;
	do {
		chime_cpu_wait();
	} while (!rtc_rx_done);
	/* read from I2C */
	chime_comm_read(I2C_RTC_COMM, rtm, sizeof(struct rtc_tm));
}

struct rtc_sync {
	bool master;
	int sec;
	uint64_t ts;
	int64_t dt; 
	float err; 
	float derr; 
	float ierr; 
	float adj; 
	float offs; 
};

double clk_val = 0;
double syn_val = 0;
int rtc_clk_var;
int rtc_syn_var;

static __thread struct rtc_sync rtcsync;


static __thread struct {
	int32_t itv_us;
	int32_t err;
} rtc_pool;

#define ERR_MAX_US 1000

void rtc_poll_isr(void)
{
	struct rtc_tm rtm;
	int32_t itv_us;
	float e;
	float de;
	float adj;
	uint64_t ts;
	int sec;

//	itv_us = rtc_pool.itv_us + rand() % 1000 - rtc_pool.err;

	/* random polling delay */
	rtc_pool.err = (rand() % ERR_MAX_US) - ERR_MAX_US / 2;
	itv_us = rtc_pool.itv_us + rtc_pool.err;
	chime_tmr_reset(0, itv_us, 0);

	/* pool the RTC */
	rtc_clock_read(&rtm);

	/* check whether we crossed a second or not */
	sec = rtm.seconds;

	if (sec != rtcsync.sec) {
		int64_t offs_max = CLK_SECS(1.0 / RTC_POOL_FREQ_HZ);
		int64_t offs;
		struct tm tm;
		time_t now;

		rtcsync.sec = sec;

		DBG1("%02d-%02d-%04d %2d:%02d:%02d", 
			 rtm.date, rtm.month, rtm.year,
			 rtm.hours, rtm.minutes, rtm.seconds);

		/* convert to UNIX time */
		tm.tm_sec = rtm.seconds;
		tm.tm_min = rtm.minutes;
		tm.tm_hour = rtm.hours;
		tm.tm_mday = rtm.date;
		tm.tm_mon = rtm.month - 1;
		tm.tm_year = rtm.year;
		tm.tm_wday = rtm.day - 1;

		/* convert to UNIX time */
		now = cs_mktime(&tm);
		ts = ((uint64_t)now << 32); /* fraction is 0 */

		/* record the time change */
		clk_val = TS2D(ts);
	//	chime_var_rec(rtc_clk_var, clk_val - chime_cpu_time());
//		chime_var_rec(rtc_clk_var, clk_val);

		offs = ts - rtcsync.ts;
		if ((offs > offs_max) || (offs < -offs_max)) {
			/* step, assume current clock and zero the error */

			DBG("<%d> !step! clk: %"PRIu64" offs=%.4f", 
				chime_cpu_id(), rtcsync.ts, TS2D(offs));

			rtcsync.ts = ts;
			rtcsync.offs = 0;
			rtcsync.err = 0;
			rtcsync.ierr = 0;

			goto done;
		} else {
			rtcsync.offs = TS2D(offs);
			DBG("<%d> clk: %"PRIu64" offs=%.4f", 
				chime_cpu_id(), rtcsync.ts, rtcsync.offs);
		}

	}

#define KP 0.0
#define KD -0.08
#define KI 0.005

	/* exponential error amortization */
	e = rtcsync.offs;

	de = e - rtcsync.err;
	rtcsync.err = e;
	rtcsync.derr = de;

	adj = KP * rtcsync.err;

	rtcsync.ierr += e;
	if (rtcsync.ierr > (1.0 / RTC_POOL_FREQ_HZ)) {
		rtcsync.ierr = (1.0 / RTC_POOL_FREQ_HZ);
	} else if (rtcsync.ierr < (-1.0 / RTC_POOL_FREQ_HZ)) {
		rtcsync.ierr = (-1.0 / RTC_POOL_FREQ_HZ);
	}

//	+ KD * rtcsync.derr + ;

	adj += KI * rtcsync.ierr;

	rtcsync.offs -= adj;

	/* update the clock */
	rtcsync.ts += rtcsync.dt + D2TS(adj);

done:
	/* record the time */
	syn_val = TS2D(rtcsync.ts);
	chime_var_rec(rtc_syn_var, syn_val - chime_cpu_time());
	chime_var_rec(rtc_clk_var, clk_val - chime_cpu_time());
}

void rtc_sync_init(bool master)
{
	rtcsync.master = master;
	rtcsync.sec = -1; /* invalid second to ckick off the algorithm */
	rtcsync.err = 0;
	rtcsync.ts = 0;
	rtcsync.dt = D2TS(1.0 / RTC_POOL_FREQ_HZ);

	DBG("<%d> dt=%"PRId64, chime_cpu_id(), rtcsync.dt);

	/* open simulation variable recorders */
	rtc_syn_var = chime_var_open("rtc_syn");
	rtc_clk_var = chime_var_open("rtc_clk");

	/* RTC timer */
	rtc_pool.itv_us = 1000000 / RTC_POOL_FREQ_HZ;
	rtc_pool.err = 0;
	chime_tmr_init(0, rtc_poll_isr, rtc_pool.itv_us / 2, 0);
}

uint64_t rtc_timestamp(void)
{
	return rtcsync.ts;
}

/****************************************************************************
 * Main CPU simulation
 ****************************************************************************/

void sim_end_isr(void)
{
	INF("...");	
	chime_sim_vars_dump();
	chime_cpu_self_destroy();
}

void cpu_master(void)
{
	tracef(T_DBG, "Master reset...");

	/* I2C slave */
	chime_comm_attach(I2C_RTC_COMM, "i2c_rtc", i2c_rcv_isr, NULL, NULL);

	rtc_sync_init(true);

	chime_tmr_init(5, sim_end_isr, 196000000, 0);

	for (;;) {
		chime_cpu_wait();
	}
}

void cleanup(void)
{
//	chime_client_stop();
}

int main(int argc, char *argv[]) 
{
	/* intialize simulation client */
	if (chime_client_start("chronos") < 0) {
		ERR("chime_client_start() failed!");	
		return 1;
	}

	/* intialize application */
	chime_app_init(cleanup);

	/* intialize RTC simulator */
	if (rtc_sim_init() < 0) {
		ERR("rtc_sim_init() failed!");	
		chime_client_stop();
		return 3;
	}

	if (chime_cpu_create(-5000, -0.5, cpu_master) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	tracef(T_DBG, "reset all cpus.");
	chime_reset_all();

	INF("chime_except_catch()...");	

	chime_except_catch(NULL);

	INF("chime_client_stop()...");	

	chime_client_stop();

	return 0;
}


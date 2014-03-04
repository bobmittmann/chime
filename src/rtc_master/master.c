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

static struct clock local_clock;

void clock_test(void)
{
	struct clock * clk = &local_clock;
	int32_t in;
	int32_t out;
	float din;
	float dout;

	for (din = -0.9; din < 0.9; din += 0.05) {
		in = Q31(din);
		out = clock_freq_adjust(clk, in, 0);
		dout = Q31F(out);
		printf("in=%d out=%d din=%f dout=%f\n", in, out, din, dout);
	}

}

void clock_timer_isr(void)
{
	DBG3("tick");

	clock_tick(&local_clock);
}

static bool pps_flag;

void pps_timer_isr(void)
{
	DBG3("tick");
	pps_flag = true;
}

void clock_pps_wait(void) 
{
	local_clock.pps_flag = false;
//	pps_flag = false;
	do {
		chime_cpu_wait();
	} while (!local_clock.pps_flag);
//	} while (!pps_flag);
}

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

uint64_t rtc_timestamp(void)
{
	struct rtc_tm rtm;
	struct tm tm;
	uint64_t ts;
	time_t now;
	int sec;	

	rtc_clock_read(&rtm);

	/* pool the RTC, waiting for next second count */
	do {
		sec = rtm.seconds;
		rtc_clock_read(&rtm);
	} while (sec == rtm.seconds);

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

	return ts;
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

	/* I2C slave */
	chime_comm_attach(I2C_RTC_COMM, "i2c_rtc", i2c_rcv_isr, NULL);

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL);

	/* open a simulation variable recorder */
	var = chime_var_open("master_time");

	/* initialize local clock */
	clock_init(&local_clock, CLOCK_FREQ_HZ);

//	clock_test();

	/* initialize clock synchronization */
	synclk_init(&local_clock, RTC_DELAY, RTC_PRECISION);


	/* start the clock tick timer */
	period_us = 1000000 / CLOCK_FREQ_HZ;
	chime_tmr_init(0, clock_timer_isr, period_us, period_us);
	chime_tmr_init(1, pps_timer_isr, 1000000, 1000000);

	pkt.sequence = 0;

	for (;;) {
		clock_pps_wait(); 

		synclk_pps();

		if (++cnt == SYNCLK_POLL) {
			uint64_t local;
			uint64_t remote;
			int64_t diff;
			char s[3][64];
	
			remote = rtc_timestamp();
			local = clock_timestamp(&local_clock);
			diff = (int64_t)(remote - local);

			chime_var_rec(var, LFP2D(local) - chime_cpu_time());

			pkt.timestamp = local;
			chime_comm_write(ARCNET_COMM, &pkt, sizeof(pkt));

			tracef(T_INF, "clk=%s offs=%9.6f", 
				   tsfmt(s[0], local), LFP2D(diff));

			(void)s; (void)diff;
			DBG1("remote=%s local=%s diff=%s", 
				tsfmt(s[0], remote), tsfmt(s[1], local), tsfmt(s[2], diff));

//			synclk_receive(remote, local);

			cnt = 0;
		}
	}
}

int main(int argc, char *argv[]) 
{
	struct comm_attr attr = {
		.wr_cyc_per_byte = 2,
		.wr_cyc_overhead = 4,
		.rd_cyc_per_byte = 2,
		.rd_cyc_overhead = 4,
		.bits_overhead = 6,
		.bits_per_byte = 11,
		.bytes_max = 256,
		.speed_bps = 625000, /* bits per second */
//		.jitter = 0.2, /* seconds */
//		.delay = 0.05  /* minimum delay in seconds */
		.jitter = 0.05, /* seconds */
		.delay = 0.125  /* minimum delay in seconds */
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

	if (rtc_sim_init() < 0) {
		chime_client_stop();
		return 3;
	}

	if (chime_cpu_create(50, -0.5, cpu_master) < 0) {
		ERR("chime_cpu_create() failed!");	
		chime_client_stop();
		return 3;
	}

	chime_reset_all();

	chime_except_catch(NULL);

	chime_client_stop();

	return 0;
}


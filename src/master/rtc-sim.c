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
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include "chime.h"
#include "debug.h"
#include "rtc-sim.h"
#include "synclk.h"

#ifndef ENABLE_RTC_TRACE
#define ENABLE_RTC_TRACE 1
#endif

/****************************************************************************
 * RTC chip simulation
 ****************************************************************************/

enum {
	REG_SECONDS = 0, /* 00-59 */
	REG_MINUTES = 1, /* 00-59 */
	REG_HOURS   = 2, /* 00-23 */
	REG_DAY     = 3, /* 1-7 */
	REG_DATE    = 4, /* 1-31 */
	REG_MONTH   = 5,  /* 1-12 */
	REG_YEAR    = 6  /* 00-99 */
};

static struct {
	union {
		uint8_t reg[16];
		struct {
			uint8_t seconds;
			uint8_t minutes;
			uint8_t hours;
			uint8_t day;
			uint8_t date;
			uint8_t month;
			uint8_t year;
		};
	};
	uint8_t addr;
#if ENABLE_RTC_TRACE
	/* simulation trace */
	int time_var;
	double time_val;
#endif
} rtc_chip;

/* Pulse per second RTC interrupt */
void rtc_pps_isr(void)
{
	const uint8_t mday_lut[12] = {
		31, 28, 31, 30, 31, 30, 
		31, 31, 30, 31, 30, 31
	};

	DBG1("<%d> PPS", chime_cpu_id());

#if ENABLE_RTC_TRACE
	rtc_chip.time_val += 1.0;
	chime_var_rec(rtc_chip.time_var, rtc_chip.time_val - chime_cpu_time());
//	chime_var_rec(rtc_chip.time_var, rtc_chip.time_val);
#endif

	/* count seconds */
	if (++rtc_chip.seconds < 60)
		return;
	rtc_chip.seconds = 0;

	/* count minutes */
	if (++rtc_chip.minutes < 60)
		return;
	rtc_chip.minutes = 0;

	/* count hours */
	if (++rtc_chip.hours < 24)
		return;
	rtc_chip.hours = 0;
	
	/* count days of the week */
	rtc_chip.day += (rtc_chip.day == 7) ? 1 : -6;

	/* count days of the month */
	if (rtc_chip.date == mday_lut[rtc_chip.month - 1]) {
		if (rtc_chip.month == 12) {
			rtc_chip.month = 1;
			rtc_chip.year++;
		} else {
			rtc_chip.month++;
		}
	} else {
		rtc_chip.date++;
	}

	INF("%02d-%02d-%04d", rtc_chip.date, rtc_chip.month, rtc_chip.year);

}

/* I2C communication */
void rtc_i2c_rcv_isr(void)
{
	uint8_t pkt[16];
	int len;

	len = chime_comm_read(0, pkt, 1024);

	assert(len > 0);

	if (pkt[0] & 0x01) {
		/* read */
		DBG1("I2C read len=%d...", len);
		/* FIXME: this is a poor modeling of the I2C */
		chime_comm_write(0, rtc_chip.reg, 8);
	} else {
		/* write */
		int n;
		INF("I2C write len=%d...", len);
		/* FIXME: this is a poor modeling of the I2C */
		if (len == 1)
			return;
		rtc_chip.addr = pkt[1];
		n = len - 2;
		memcpy(&rtc_chip.reg[rtc_chip.addr], &pkt[2], n);
		rtc_chip.addr += n;
	}
}

void rtc_chip_reset(void)
{
	tracef(T_DBG, "RTC chip reset.");
	INF("RTC chip:%d reset", chime_cpu_id());

	/* Wed, 01 Jan 2014 00:00:00 GMT */
	rtc_chip.addr = 0;
	rtc_chip.seconds = 1;
	rtc_chip.minutes = 0;
	rtc_chip.hours = 0;
#if 0
	rtc_chip.date = 1;
	rtc_chip.month = 1;
	rtc_chip.day = 4;
	rtc_chip.year = 14;
#else
	rtc_chip.date = 1;
	rtc_chip.month = 1;
	rtc_chip.day = 5;
	rtc_chip.year = 70;
#endif

#if ENABLE_RTC_TRACE
	rtc_chip.time_val = ((rtc_chip.hours * 60) + 
						 rtc_chip.minutes) * 60 + rtc_chip.seconds;
	chime_var_rec(rtc_chip.time_var, rtc_chip.time_val - chime_cpu_time());
#endif

	/* PPS timer */
	chime_tmr_init(0, rtc_pps_isr, 300000, 1000000);
	/* I2C slave */
	chime_comm_attach(0, "i2c_rtc", rtc_i2c_rcv_isr, NULL, NULL);

	for (;;) {
		chime_cpu_wait();
	}
}

int rtc_sim_init(void)
{
	/* I2C communication setup */
	struct comm_attr attr = {
		.wr_cyc_per_byte = 1,
		.wr_cyc_overhead = 2,
		.rd_cyc_per_byte = 1,
		.rd_cyc_overhead = 2,
		.bits_overhead = 0,
		.bits_per_byte = 9,
		.nodes_max = 2,
		.bytes_max = 16,
		.speed_bps = 100000, /* 100khz */
		.max_jitter = 0, /* seconds */
		.min_delay = 0,  /* minimum delay in seconds */
		.nod_delay = 0, /* per node delay in seconds */
		.hist_en = false, /* enable distribution histogram */
		.txbuf_en = true, /* enable xmit buffer */
		.dcd_en = false, /* enable data carrier detect */
		.exp_en = false /* enable exponential distribution */
	};
	
	/* create the I2C communication channel */
	if (chime_comm_create("i2c_rtc", &attr) < 0) {
		DBG1("chime_comm_create() failed!");	
	}

#if ENABLE_RTC_TRACE
	if ((rtc_chip.time_var = chime_var_open("rtc_time")) < 0) {
		DBG1("chime_var_open() failed!");	
		return -1;
	}
#endif

	/* create a CPU to simulate the RTC */
//	if (chime_cpu_create(3.5, -0.001, rtc_chip_reset) < 0) {
	if (chime_cpu_create(0, 0, rtc_chip_reset) < 0) {
		ERR("chime_cpu_create() failed!");	
		return -1;
	}

	return 0;
}


/****************************************************************************
 * Local RTC chip connection 
 ****************************************************************************/

static bool rtc_rx_done;
static unsigned int rtc_comm;

/* This ISR is called when data from the RTC is received on the I2c */
void i2c_rcv_isr(void)
{
	DBG3("rtc_rx_done!");
	rtc_rx_done = true;;
}

int rtc_read(struct rtc_tm * rtm)
{
	uint8_t pkt[2];

	/* request I2C read */
	pkt[0] = 0x01; 
	chime_comm_write(rtc_comm, pkt, 1);
	/* wait for I2C interrupt */
	rtc_rx_done = false;
	do {
		chime_cpu_wait();
	} while (!rtc_rx_done);

	/* read from I2C */
	return chime_comm_read(rtc_comm, rtm, sizeof(struct rtc_tm));
}

int rtc_connect(int comm) 
{
	rtc_comm = comm;
	rtc_rx_done = false;

	/* I2C slave */
	return chime_comm_attach(rtc_comm, "i2c_rtc", i2c_rcv_isr, NULL, NULL);
}

uint64_t rtc_timestamp(struct rtc_tm * rtm)
{
	struct tm tm;
	uint64_t ts;
	time_t now;

	/* convert to UNIX time */
	tm.tm_sec = rtm->seconds;
	tm.tm_min = rtm->minutes;
	tm.tm_hour = rtm->hours;
	tm.tm_mday = rtm->date;
	tm.tm_mon = rtm->month - 1;
	tm.tm_year = rtm->year;
	tm.tm_wday = rtm->day - 1;

	/* convert to UNIX time */
	now = cs_mktime(&tm);
	ts = ((uint64_t)now << 32); /* fraction is 0 */

	return ts;
}


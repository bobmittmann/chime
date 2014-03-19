#ifndef __SYNCLK_H__
#define __SYNCLK_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "clock.h"

/* 
   Clock FLL synchronization 
 */

struct clock_fll {
	int32_t clk_err;
	int32_t clk_phi; /* phase error */
	int32_t clk_acc; /* phase error */
	int32_t clk_drift;
	int32_t drift_err;
	uint64_t ref_ts;
	uint64_t clk_ts;
	int32_t err_max;
	int32_t edge_offs;
	uint32_t edge_filt;
	uint32_t edge_jit;
	bool lock;
	bool run;
};

/* 
   Clock PLL synchronization 
 */

struct clock_pll {
	int32_t clk_err;
	int32_t drift;
	int32_t err;
	int32_t ref;
	int32_t offs;
	int32_t ierr;
	int64_t itvl; /* interval between the last two samples */
	bool lock;
	bool run;
	struct {
		int32_t x[4];
		int32_t y[4];
	} f0;
	struct {
		int32_t x[4];
		int32_t y[4];
	} f1;
};


/* 
   Clock filter 
 */

#define CLK_OFFS_INVALID INT64_MIN
#define CLK_FILT_LEN 32

struct clock_filt {
	struct clock  * clk;
	uint32_t precision;

	bool spike; /* spike indication */
	int64_t offs; /* last offset value */
	int32_t average; /* moving average */
	int32_t variance; /* moving variance */
	int64_t sx1;
	int32_t sx2;
	unsigned int len;
	int32_t x[CLK_FILT_LEN];

	struct {
		int32_t delay;
		uint32_t precision;
		uint64_t timestamp; /* last received timestamp */
	} peer;

	struct {
		uint64_t spike;
		uint64_t drop;
		uint64_t step;
	} stat;
};

struct synclk_pkt {
	uint32_t sequence;
	int32_t precision;
	uint64_t timestamp;
};

#define SYNCLK_POLL (1 << 5) /* poll interval time (32 s) */

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Clock PLL (Phase Locked Loop) functions 
 ****************************************************************************/

void pll_reset(struct clock_pll  * pll);

void pll_init(struct clock_pll  * pll);

void pll_step(struct clock_pll  * pll);

void pll_phase_adjust(struct clock_pll  * pll, int64_t offs, int64_t itvl);

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) functions 
 ****************************************************************************/

void fll_step(struct clock_fll  * fll, uint64_t ref_ts, int64_t offs);

void fll_reset(struct clock_fll  * fll, uint64_t ref_ts);

void fll_init(struct clock_fll  * fll, int64_t err_max);

/****************************************************************************
 * Clock filter functions 
 ****************************************************************************/

/* Initialize the clock network filter */
void filt_init(struct clock_filt * filt, uint32_t precision);

/* Reset the clock network filter.
   - peer_delay: average network delay (CLK format)
   - peer_precision: precison of this clock (CLK format) */
void filt_reset(struct clock_filt * filt, int32_t peer_delay, 
				int32_t peer_precision);

/* Called when a time packed is received from the network */
int64_t filt_receive(struct clock_filt * filt, 
					 uint64_t remote, uint64_t local);

/****************************************************************************
 * Utility functions 
 ****************************************************************************/

/* Format a clock timetamp into a string with upt ot microsseconds resolution.
   The string has to be at least 20 characters long.
   The fields to be printed are adjusted according to their content... */
char * fmt_clk(char * s, int64_t ts);
#define FMT_CLK(TS) fmt_clk(({char __s[20]; __s;}), (int64_t)(TS))

/* Format a clock timetamp into a string with milliseconds resolution.
   The string has to be at least 18 characters long */
char * fmt_clk_ms(char * s, int64_t ts);
#define FMT_CLK_MS(TS) fmt_clk_ms(({char __s[20]; __s;}), (int64_t)(TS))

/* Format a clock timetamp into a string with microsseconds resolution.
   The string has to be at least 20 characters long */
char * fmt_clk_us(char * s, int64_t ts);
#define FMT_CLK_US(TS) fmt_clk_us(({char __s[20]; __s;}), (int64_t)(TS))

/* Format a Q31 number using 3 decimal places */
char * fmt_q31_3(char * s, int32_t x);
#define FMT_Q31_3(X) fmt_q31_3(({char __s[8]; __s;}), (int32_t)(X))

/* Format a Q31 number using 6 decimal places */
char * fmt_q31_6(char * s, int32_t x);
#define FMT_Q31_6(X) fmt_q31_3(({char __s[12]; __s;}), (int32_t)(X))

/* Format a Q31 number using 9 decimal places */
char * fmt_q31_9(char * s, int32_t x);
#define FMT_Q31_9(X) fmt_q31_9(({char __s[12]; __s;}), (int32_t)(X))

#ifdef __cplusplus
}
#endif	

#endif /* __SYNCLK_H__ */


#ifndef __SYNCLK_H__
#define __SYNCLK_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


/* Convert from float point to clock interval */
#define FLOAT_CLK(X)     ((int64_t)((X) * 4294967296.))
/* Convert from clock interval to float point */
#define CLK_FLOAT(X)     ((float)(int64_t)(X) / 4294967296.)
/* Convert from clock interval to double float point */
#define CLK_DOUBLE(X)    ((double)(int64_t)(X) / 4294967296.)
/* convert from clock interval to microsseconds */
#define CLK_US(X)        (((int64_t)(X) * 1000000LL) / 4294967296LL)
/* convert from clock interval to milliseconds */
#define CLK_MS(X)        (((int64_t)(X) * 1000LL) / 4294967296LL)
/* convert from clock interval to milliseconds */
#define CLK_SEC(X)       ((int32_t)((int64_t)(X) / 4294967296LL))

/* Q31.32 multiplication */
#define CLK_MUL(X1, X2)   (((int64_t)(X1) * (int64_t)(X2)) >> 32)
/* Q31.32 division */
#define CLK_DIV(NUM, DEN) (((int64_t)(NUM) << 32) / (int32_t)(DEN))


/* Convert from fixed point Q1.31 to clock interval */
#define Q31_CLK(X)       ((int64_t)((X) * 2))
/* Convert from clock interval to fixed point Q1.31 */
#define CLK_Q31(X)       ((int32_t)(((X) + 1) / 2)) 
/* Convert from fixed point Q1.31 to float point */
#define Q31_FLOAT(X)     ((float)(X) / 2147483648.)
/* Convert from float point to fixed point Q1.31 */
#define FLOAT_Q31(X)     ((int32_t)((X) * 2147483648.))
/* Q1.31 multiplication */
#define Q31_MUL(X1, X2)   (((int64_t)(X1) * (int64_t)(X2)) >> 31)
/* Q1.31 division */
#define Q31_DIV(NUM, DEN) (((int64_t)(NUM) << 31) / (int32_t)(DEN))


#define CLK_OFFS_INVALID INT64_MIN


struct clock {
	volatile uint64_t timestamp; /* clock timestamp */
	uint64_t offset; /* clock offset */
	uint32_t resolution; /* fractional clock resolution */
	uint32_t increment; /* fractional per tick increment */
	int32_t n_freq; /* frequency itegral part */
	int32_t q_freq; /* frequency decimal part */
	int32_t drift_comp; /* drift compensation seconds per ticks */
	int32_t jitter;
	int64_t tmr_k; /* multiplication factor for hw timer ticks conversion */
	float tmr_fk; /* multiplication factor for hw timer ticks conversion */
	bool pps_flag;
	int hw_tmr;
};

/* 
   Clock FLL synchronization 
 */

struct clock_fll {
	struct clock  * clk;
	int32_t clk_err;
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
	struct clock  * clk;
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
		int32_t precision;
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
 * Clock functions 
 ****************************************************************************/

/* Update the clock time.
   This function should be called periodically by an
   interrupt handler. */
bool clock_tick(struct clock * clk);

/* Return the uncorrected (raw) clock timestamp */
uint64_t clock_timestamp(struct clock * clk);

/* Return the corrected (offset) clock time */
uint64_t clock_time_get(struct clock * clk);

/* Set the clock to the specified timestamp 'ts'. 
   This will adjust the clock offset only. */ 
void clock_time_set(struct clock * clk, uint64_t ts);

/* Step the clock by the specified interval 'dt'. 
   This will adjust the clock offset only. */ 
void clock_step(struct clock * clk, int64_t dt);

/* Adjust the clock frequency */ 
int32_t clock_drift_comp(struct clock * clk, int32_t drift, int32_t est_err);

/* Initialize the clock */ 
void clock_init(struct clock * clk, uint32_t tick_itvl, int hw_tmr);


/****************************************************************************
 * Clock PLL (Phase Locked Loop) functions 
 ****************************************************************************/

void pll_reset(struct clock_pll  * pll);

void pll_init(struct clock_pll  * pll, struct clock  * clk);

void pll_step(struct clock_pll  * pll);

void pll_phase_adjust(struct clock_pll  * pll, int64_t offs, int64_t itvl);

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) functions 
 ****************************************************************************/

void fll_step(struct clock_fll  * fll, uint64_t ref_ts, int64_t offs);

void fll_reset(struct clock_fll  * fll, uint64_t ref_ts);

void fll_init(struct clock_fll  * fll, struct clock  * clk, int64_t err_max);

/****************************************************************************
 * Clock filter functions 
 ****************************************************************************/

/* Initialize the clock network filter */
void filt_init(struct clock_filt * filt, struct clock  * clk);

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

#define FMT_US 0 
#define FMT_S  1
#define FMT_M  2
#define FMT_H  3

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

/*
 * Converts the calendar time pointed to by tim_p into a broken-down time
 * expressed as local time. Returns a pointer to a structure containing the
 * broken-down time.
 */
struct tm * cs_gmtime_r(const time_t * tim_p, struct tm * res);


/*
 * Converts the broken-down time, expressed as local time, in the structure
 * pointed to by __tm into a calendar time value. The original values of the
 * tm_wday and tm_yday fields of the structure are ignored, and the original
 * values of the other fields have no restrictions. On successful completion
 * the fields of the structure are set to represent the specified calendar
 * time. Returns the specified calendar time. If the calendar time can not be
 * represented, returns the value (time_t) -1.
 */
time_t cs_mktime(struct tm *__tm);

#ifdef __cplusplus
}
#endif	

#endif /* __SYNCLK_H__ */


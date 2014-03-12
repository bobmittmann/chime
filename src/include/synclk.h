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
#define CLK_SEC(X)       ((int64_t)(X) / 4294967296LL)
/* Convert from fixed point Q31 to clock interval */
#define Q31_CLK(X)       ((int64_t)((X) * 2))
/* Convert from clock interval to fixed point Q31 */
#define CLK_Q31(X)       ((int32_t)(((X) + 1) / 2)) 
/* Convert from fixed point Q31 to float point */
#define Q31_FLOAT(X)     ((float)(X) / 2147483648.)
/* Convert from float point to fixed point Q31 */
#define FLOAT_Q31(X)     ((int32_t)((X) * 2147483648.))

#define Q31_MUL(X1, X2)   (((int64_t)(X1) * (int64_t)(X2)) >> 31)
#define Q31_DIV(NUM, DEN) (((int64_t)(NUM) << 31) / (int32_t)(DEN))


struct clock_pll {
	int32_t err;
	int32_t freq;
	int32_t vco;
	struct {
		int32_t x[4];
		int32_t y[4];
	} f0;
	struct {
		int32_t x[4];
		int32_t y[4];
	} f1;
};

struct clock_fll {
	int32_t drift;
	uint64_t ref_ts;
	uint64_t clk_ts;
	int64_t err;
	int32_t edge_offs;
	uint32_t edge_filt;
	bool lock;
	bool run;
};

struct clock {
	volatile uint64_t timestamp; /* clock timestamp */
	uint64_t offset; /* clock offset */
	int32_t resolution; /* fractional clock resolution */
	int32_t increment; /* fractional per tick increment */
	int32_t drift_comp;
	uint32_t frequency;
	bool pps_flag;
};

struct synclk_pkt {
	uint32_t sequence;
	uint32_t reserved;
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
int32_t clock_drift_comp(struct clock * clk, int32_t drift);

/* Initialize the clock */ 
void clock_init(struct clock * clk, uint32_t tick_freq_hz);


/****************************************************************************
 * Clock PLL (Phase Locked Loop) functions 
 ****************************************************************************/

int32_t pll_phase_adjust(struct clock_pll * pll, int32_t offs, 
						 int32_t interval);

int32_t pll_freq_adjust(struct clock_pll * pll, 
						int32_t freq_adj, int32_t offs);

void pll_reset(struct clock_pll  * pll);

/****************************************************************************
 * Clock FLL (Frequency Locked Loop) functions 
 ****************************************************************************/

void fll_reset(struct clock_fll  * fll);


/****************************************************************************
 * Clock synchronization functions 
 ****************************************************************************/

void synclk_init(struct clock * clk, float peer_delay, float peer_precision);

void synclk_receive(uint64_t local, uint64_t remote);

void synclk_pps(void);


/****************************************************************************
 * Utility functions 
 ****************************************************************************/

#define FMT_US 0 
#define FMT_S  1
#define FMT_M  2
#define FMT_H  3

char * fmt_clk_opt(char * s, int64_t ts, int opt);


/* Format a clock timetamp into a string with upt ot microsseconds resolution.
   The string has to be at least 20 characters long.
   The fields to be printed are adjusted according to their content... */
char * fmt_clk(char * s, int64_t ts);

/* Format a clock timetamp into a string with milliseconds resolution.
   The string has to be at least 18 characters long */
char * fmt_clk_ms(char * s, int64_t ts);

/* Format a clock timetamp into a string with microsseconds resolution.
   The string has to be at least 20 characters long */
char * fmt_clk_us(char * s, uint64_t ts);

/* Format a Q31 number using 3 decimal places */
char * fmt_q31_3(char * s, int32_t x);

/* Format a Q31 number using 6 decimal places */
char * fmt_q31_6(char * s, int32_t x);

/* Format a Q31 number using 9 decimal places */
char * fmt_q31_9(char * s, int32_t x);

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


#ifndef __SYNCLK_H__
#define __SYNCLK_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define FRAC       4294967296.             /* 2^32 as a double */
#define D2LFP(a)   ((int64_t)((a) * FRAC))  /* NTP timestamp */
#define LFP2D(a)   ((double)(a) / FRAC)

#define TS2D(X)   ((double)(X) / FRAC)
#define D2TS(X)   ((uint64_t)((X) * FRAC))

#define DT2D(X)   ((double)(int64_t)(X) / FRAC)
#define D2DT(X)   ((int64_t)((X) * FRAC))


#define D2FRAC(a)   ((int32_t)((a) * (1 << 31))  
#define FRAC2D(a)   ((double)(a) / (1 << 31))


#define DT2FLOAT(X)     ((float)(int64_t)(X) / FRAC)
#define FLOAT2DT(X)     ((int64_t)((X) * FRAC))

#define FLOAT2ITV(X)     ((int64_t)((X) * 4294967296.))
#define ITV2FLOAT(X)     ((float)(int64_t)(X) / 4294967296.)

#define FLOAT2FRAC(X)   ((int32_t)((X) * 2147483648.)) 
#define FRAC2FLOAT(X)   ((float)(X) / 2147483648.)


/* Convert from float point to clock */
#define FLOAT_CLK(X)     ((int64_t)((X) * 4294967296.))
/* Convert from clock to float point */
#define CLK_FLOAT(X)     ((float)(int64_t)(X) / 4294967296.)
/* Convert from fixed point Q31 to clock */
#define Q31_CLK(X)       ((int64_t)((X) * 2))
/* Convert from clock to fixed point Q31 */
#define CLK_Q31(X)       ((int32_t)(((X) + 1) / 2)) 
/* Convert from fixed point Q31 to float point */
#define Q31_FLOAT(X)     ((float)(X) / 2147483648.)
/* Convert from float point to fixed point Q31 */
#define FLOAT_Q31(X)     ((int32_t)((X) * 2147483648.))

#define Q31F(X)          ((float)(X) / 2147483648.)
#define Q31(X)           ((int32_t)((X) * 2147483648.))
#define Q31MUL(X1, X2)   (((int64_t)(X1) * (int64_t)(X2)) >> 31)
#define Q31DIV(NUM, DEN) (((int64_t)(NUM) << 31) / (int32_t)(DEN))



#define CLK_SECS(X)  ((int64_t)((X) * 4294967296.))

#define ARCNET_COMM 0
#define I2C_RTC_COMM 1

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

struct clock {
	volatile uint64_t timestamp;
	uint64_t offset;
	int32_t resolution;
	int32_t increment;
	int32_t drift_comp;
	uint32_t frequency;
	
	struct clock_pll pll;

	bool pps_flag;
};

#define SYNCLK_POLL (1 << 5) /* poll interval time (32 s) */
//#define SYNCLK_POLL (1 << 4) /* poll interval time (16 s) */

struct synclk_pkt {
	uint32_t sequence;
	uint32_t reserved;
	uint64_t timestamp;
};

#ifdef __cplusplus
extern "C" {
#endif

void clock_init(struct clock * clk, uint32_t tick_freq_hz);

bool clock_tick(struct clock * clk);

uint64_t clock_timestamp(struct clock * clk);

int32_t clock_freq_adjust(struct clock * clk, int32_t freq_adj, 
						  int32_t offs_adj);

int32_t clock_drift_adjust(struct clock * clk, int32_t drift);

void clock_step(struct clock * clk, uint64_t ts);

void clock_offs_adjust(struct clock * clk, int64_t offs_adj);

int32_t clock_phase_adjust(struct clock * clk, int32_t offs, int32_t interval);

void synclk_init(struct clock * clk, float peer_delay, float peer_precision);

void synclk_receive(uint64_t local, uint64_t remote);

void synclk_pps(void);

int ts_sprint(char * s, uint64_t ts);

char * tsfmt(char * s, uint64_t ts);

void pll_reset(struct clock_pll  * pll);

int32_t pll_pps_step(struct clock_pll  * pll, int32_t offs);

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


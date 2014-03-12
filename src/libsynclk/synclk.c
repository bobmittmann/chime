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
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>

#include "debug.h"
#include "synclk.h"
#include "chime.h"

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

#define CLK_SECS(X)  ((int64_t)((X) * 4294967296.))


#ifndef ENABLE_FREQ_ADJ
#define ENABLE_FREQ_ADJ 0
#endif

#ifndef MIN
#define	MIN(a,b)	(((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define	MAX(a,b)	(((a) > (b)) ? (a) : (b))
#endif

#define SQUARE(x) ((x) * (x))
#define SQRT(x) (sqrt(x))
#define DIFF(x, y) (SQUARE((x) - (y)))

/*
 * Clock filter algorithm tuning parameters
 */
#define MAXDISPERSE	16.	/* max dispersion */
#define	SYNC_FILTER_LEN 8	/* clock filter stages */
#define NTP_FWEIGHT	.5	/* clock filter weight */

/*
 * Selection algorithm tuning parameters
 */
#define MAXDISTANCE	1.5	/* max root distance (select threshold) */
#define CLOCK_SGATE	3.	/* popcorn spike gate */

/*
 * Values for peer.leap, sys_leap
 */
#define	LEAP_NOWARNING	0x0	/* normal, no leap second warning */
#define	LEAP_ADDSECOND	0x1	/* last minute of day has 61 seconds */
#define	LEAP_DELSECOND	0x2	/* last minute of day has 59 seconds */
#define	LEAP_NOTINSYNC	0x3	/* overload, clock is free running */

/*
 * This is an implementation of the clock discipline algorithm described
 * in UDel TR 97-4-3, as amended. It operates as an adaptive parameter,
 * hybrid phase/frequency-lock loop. A number of sanity checks are
 * included to protect against timewarps, timespikes and general mayhem.
 * All units are in s and s/s, unless noted otherwise.
 */
#define CLOCK_MAX	.128	/* default step threshold (s) */
//#define CLOCK_MINSTEP	900.
#define CLOCK_MINSTEP	64.
#define	CLOCK_PHI	15e-6	/* max frequency error (s/s) */
#define CLOCK_PLL	16.	/* PLL loop gain (log2) */
#define CLOCK_AVG	8.	/* parameter averaging constant */
#define	CLOCK_ALLAN	(1 << 11) /* Allan intercept (s) */
#define CLOCK_LIMIT	30	/* poll-adjust threshold */
#define CLOCK_PGATE	4.	/* poll-adjust gate */
#define	FREQTOD(x)	((x) / 65536e6) /* NTP to double */ 
#define	DTOFREQ(x)	((int32)((x) * 65536e6)) /* double to NTP */

#define NTP_MAXFREQ	500e-6

#define SYS_POLL (1 << 6) /* default min poll (64 s) */

/*
 * System event codes
 */
#define	EVNT_UNSPEC	0	/* unspecified */
#define	EVNT_NSET	1	/* freq not set */
#define	EVNT_SPIK	2	/* spike detect */
#define	EVNT_FREQ	3	/* freq mode */
#define	EVNT_SYNC	4	/* clock sync */
#define	EVNT_CLOCKRESET	5 /* clock step */

static const char __st_nm[][8] = {
	"UNSPEC", /* unspecified */
	"NSET",   /* freq not set */
	"SPIK",   /* spike detect */
	"FREQ",   /* freq mode */
	"SYNC",   /* clock sync */
	"STEP"    /* clock step */
};

struct synclk_filter {
	uint32_t nextpt;
	double delay[SYNC_FILTER_LEN]; /* delay shift register */
	double offset[SYNC_FILTER_LEN]; /* offset shift register */
	double disp[SYNC_FILTER_LEN]; /* dispersion shift register */
	uint32_t epoch[SYNC_FILTER_LEN]; /* epoch shift register */
	uint8_t order[SYNC_FILTER_LEN]; /* filter sort index */
};

struct synclk_peer {
	struct synclk_filter filter;
	uint64_t timestamp; /* last timestamp */
	uint32_t update;    /* last update (s) */
	uint32_t epoch;		/* reference epoch */
	float precision;    /* clock precision (s) */
	double offset;		/* peer clock offset */
	double delay;		/* peer roundtrip delay */
	double jitter;		/* peer jitter (squares) */
	double disp;		/* peer dispersion */
};

static struct {
	struct clock * sys_clk;
	struct clock_pll pll;

	uint32_t current_time;    /* seconds since startup */
	struct synclk_peer peer;

	uint8_t sys_leap; /* system leap indicator */

	/*
	 * Clock state machine variables
	 */
	uint8_t state;		/* clock discipline state */
	int	tc_counter;		/* jiggle counter */
	double last_offset;		/* last offset (s) */
	uint32_t last_step;	/* last clock step */
	/*
	 * Program variables
	 */
	double drift_comp;		/* frequency (s/s) */
	double clock_stability;	/* frequency stability (wander) (s/s) */
	double clock_precision; /* clock precision */
	double clock_offset;	/* offset */
	double clock_jitter;	/* offset jitter */
	uint32_t clock_epoch; /* last update */
} sync;

bool synclk_clock_filter(double offset, double delay, double dispersion)
{
	struct synclk_peer * peer = &sync.peer;
	struct synclk_filter * filter = &sync.peer.filter;
	double dst[SYNC_FILTER_LEN];		/* distance vector */
	int	ord[SYNC_FILTER_LEN];		/* index vector */
	int	i, j, k, m;
	double dtemp;
	double etemp;

	/*
	 * A sample consists of the offset, delay, dispersion and epoch
	 * of arrival. The offset and delay are determined by the on-
	 * wire protocol. The dispersion grows from the last outbound
	 * packet to the arrival of this one increased by the sum of the
	 * peer precision and the system precision as required by the
	 * error budget. First, shift the new arrival into the shift
	 * register discarding the oldest one.
	 */
	j = filter->nextpt;
	filter->offset[j] = offset;
	filter->delay[j] = delay;
	filter->disp[j] = dispersion;
	filter->epoch[j] = sync.current_time;
	j = (j + 1) % SYNC_FILTER_LEN;
	filter->nextpt = j;

	/*
	 * Update dispersions since the last update and at the same
	 * time initialize the distance and index lists. Since samples
	 * become increasingly uncorrelated beyond the Allan intercept,
	 * only under exceptional cases will an older sample be used.
	 * Therefore, the distance list uses a compound metric. If the
	 * dispersion is greater than the maximum dispersion, clamp the
	 * distance at that value. If the time since the last update is
	 * less than the Allan intercept use the delay; otherwise, use
	 * the sum of the delay and dispersion.
	 */
	dtemp = CLOCK_PHI * (sync.current_time - peer->update);
	peer->update = sync.current_time;
	for (i = SYNC_FILTER_LEN - 1; i >= 0; i--) {
		if (i != 0)
			filter->disp[j] += dtemp;
		if (filter->disp[j] >= MAXDISPERSE) { 
			filter->disp[j] = MAXDISPERSE;
			dst[i] = MAXDISPERSE;
		} else if ((peer->update - filter->epoch[j]) > CLOCK_ALLAN) {
			dst[i] = filter->delay[j] + filter->disp[j];
		} else {
			dst[i] = filter->delay[j];
		}
		ord[i] = j;
		j = (j + 1) % SYNC_FILTER_LEN;
	}

	/*
	 * If the clock discipline has stabilized, sort the samples by
	 * distance.  
	 */
	if (sync.sys_leap != LEAP_NOTINSYNC) {
		for (i = 1; i < SYNC_FILTER_LEN; i++) {
			for (j = 0; j < i; j++) {
				if (dst[j] > dst[i]) {
					k = ord[j];
					ord[j] = ord[i];
					ord[i] = k;
					etemp = dst[j];
					dst[j] = dst[i];
					dst[i] = etemp;
				}
			}
		}
	}

	/*
	 * Copy the index list to the association structure so ntpq
	 * can see it later. Prune the distance list to leave only
	 * samples less than the maximum dispersion, which disfavors
	 * uncorrelated samples older than the Allan intercept. To
	 * further improve the jitter estimate, of the remainder leave
	 * only samples less than the maximum distance, but keep at
	 * least two samples for jitter calculation.
	 */
	m = 0;
	for (i = 0; i < SYNC_FILTER_LEN; i++) {
		filter->order[i] = (uint8_t)ord[i];
		if (dst[i] >= MAXDISPERSE || (m >= 2 && dst[i] >= MAXDISTANCE))
			continue;
		m++;
	}

	/*
	 * Compute the dispersion and jitter. The dispersion is weighted
	 * exponentially by NTP_FWEIGHT (0.5) so it is normalized close
	 * to 1.0. The jitter is the RMS differences relative to the
	 * lowest delay sample.
	 */
	peer->disp = peer->jitter = 0;
	k = ord[0];
	for (i = SYNC_FILTER_LEN - 1; i >= 0; i--) {
		j = ord[i];
		peer->disp = NTP_FWEIGHT * (peer->disp + filter->disp[j]);
		if (i < m)
			peer->jitter += DIFF(filter->offset[j], filter->offset[k]);
	}

	/*
	 * If no acceptable samples remain in the shift register,
	 * quietly tiptoe home leaving only the dispersion. Otherwise,
	 * save the offset, delay and jitter. Note the jitter must not
	 * be less than the precision.
	 */
	if (m == 0)
		return true;

	etemp = fabs(peer->offset - filter->offset[k]);
	peer->offset = filter->offset[k];
	peer->delay = filter->delay[k];
	if (m > 1)
		peer->jitter /= m - 1;
	peer->jitter = MAX(SQRT(peer->jitter), sync.clock_precision);

	/*
	 * If the the new sample and the current sample are both valid
	 * and the difference between their offsets exceeds CLOCK_SGATE
	 * (3) times the jitter and the interval between them is less
	 * than twice the host poll interval, consider the new sample
	 * a popcorn spike and ignore it.
	 */
	if (peer->disp < MAXDISTANCE && 
		filter->disp[k] < MAXDISTANCE && 
		etemp > CLOCK_SGATE * peer->jitter &&
		filter->epoch[k] - peer->epoch < 2. * SYS_POLL) {
		WARN("popcorn: %.6f s", etemp);
		return false;
	}

	/*
	 * A new minimum sample is useful only if it is later than the
	 * last one used. In this design the maximum lifetime of any
	 * sample is not greater than eight times the poll interval, so
	 * the maximum interval between minimum samples is eight
	 * packets.
	 */
	if (filter->epoch[k] <= peer->epoch) {
		WARN("old sample %d", sync.current_time - filter->epoch[k]);
		return false;
	}

	peer->epoch = filter->epoch[k];

	/*
	 * The mitigated sample statistics are saved for later
	 * processing. If not synchronized or not in a burst, tickle the
	 * clock select algorithm.
	 */
	DBG1("n %d off %.6f del %.6f dsp %.6f jit %.6f", 
		 m, peer->offset, peer->delay, peer->disp, peer->jitter);

	return true;
}

/*
 * Clock state machine. Enter new state and set state variables.
 */
static void rstclock(int trans, double offset)
{
//	DBG("mu %u state %d", sync.current_time - sync.clock_epoch, trans);

#if (ENABLE_FREQ_ADJ == 0)
	if (trans == EVNT_FREQ)
		trans = EVNT_SYNC;
#endif

	if (trans != sync.state)
		DBG("[%s]->[%s]", __st_nm[sync.state], __st_nm[trans]);

	sync.state = trans;
	sync.last_offset = sync.clock_offset = offset;
	sync.clock_epoch = sync.current_time; 
}

void step_systime(double fp_offset)
{
	int64_t offs_adj;

	offs_adj = D2LFP(fp_offset);
	if (offs_adj != 0) {
		DBG3("offs adj=%f --> %" PRId64 ".", fp_offset, offs_adj);
		clock_step(sync.sys_clk, offs_adj);
	}
}

/*
 * adj_host_clock - Called once every second to update the local clock.
 *
 * LOCKCLOCK: The only thing this routine does is increment the
 * sys_rootdisp variable.
 */
void synclk_pps(void)
{
	double	adjustment;
	int64_t offs_adj;

	sync.current_time++;
	/*
	 * Implement the phase and frequency adjustments. The gain
	 * factor (denominator) increases with poll interval, so is
	 * dominated by the FLL above the Allan intercept.
 	 */
	adjustment = sync.clock_offset / (CLOCK_PLL * SYS_POLL);
	sync.clock_offset -= adjustment;

	offs_adj = D2LFP(adjustment);

	DBG5("tick");

	if (offs_adj != 0) {
		DBG3("offs adj=%f --> %" PRId64 ".", adjustment, offs_adj);
//		clock_offs_adjust(sync.sys_clk, offs_adj);
	}
}

/*
 * set_freq - set clock frequency
 */
double set_freq(double freq, double offset)
{
	int32_t freq_adj;
	int32_t drift_comp;
	int32_t offs_adj;
	double old_freq;

	sync.drift_comp = freq;

	freq_adj = FLOAT_Q31(freq);
	offs_adj = FLOAT_Q31(offset);
	
	clock_step(sync.sys_clk, offs_adj);
	drift_comp  = clock_drift_comp(sync.sys_clk, freq_adj);

	INF("drift_comp=%d.", drift_comp);

	old_freq = FRAC2D(drift_comp);

	INF("old_freq=%.7f freq=%.7f.", old_freq, freq);

	return old_freq;
}

/*
 * calc_freq - calculate frequency directly
 *
 * This is very carefully done. When the offset is first computed at the
 * first update, a residual frequency component results. Subsequently,
 * updates are suppresed until the end of the measurement interval while
 * the offset is amortized. At the end of the interval the frequency is
 * calculated from the current offset, residual offset, length of the
 * interval and residual frequency component. At the same time the
 * frequenchy file is armed for update at the next hourly stats.
 */
static double direct_freq(double fp_offset)
{
	set_freq((fp_offset - sync.clock_offset) / 
			 (sync.current_time - sync.clock_epoch) + sync.drift_comp, 
			 fp_offset);

	INF("mu=%d offs=%f drift_comp=%f", sync.current_time - sync.clock_epoch, 
		fp_offset, sync.drift_comp);

	return sync.drift_comp;
}

int synclk_local_clock(double fp_offset)
{
	double	mu;		/* interval since last update */
	double	clock_frequency; /* clock frequency */
	double	dtemp, etemp; /* double temps */
	int rval = 1;

	mu = sync.current_time - sync.clock_epoch;
	DBG2("mu=%.3f", mu); 
	clock_frequency = sync.drift_comp;
	if (fabs(fp_offset) > CLOCK_MAX) {
		WARN("fp_offs(%f) > CLOCK_MAX(%f)", fp_offset, CLOCK_MAX);
		switch (sync.state) {
		/*
		 * In SYNC state we ignore the first outlyer and switch
		 * to SPIK state.
		 */
		case EVNT_SYNC:
			INF("SPIK");
			sync.state = EVNT_SPIK;
			return (0);

		/*
		 * In FREQ state we ignore outlyers and inlyers. At the
		 * first outlyer after the stepout threshold, compute
		 * the apparent frequency correction and step the phase.
		 */
		case EVNT_FREQ:
	 		if (mu < CLOCK_MINSTEP) {
				INF("FREQ: (mu=%f < CLOCK_MINSTEP=%f)", mu, CLOCK_MINSTEP);
				return (0);
			}

			clock_frequency = direct_freq(fp_offset);

			/* fall through to S_SPIK */

		/*
		 * In SPIK state we ignore succeeding outlyers until
		 * either an inlyer is found or the stepout threshold is
		 * exceeded.
		 */
		case EVNT_SPIK:
			if (mu < CLOCK_MINSTEP) {
				INF("(mu < CLOCK_MINSTEP)");
				return (0);
			}

			/* fall through to default */

		/*
		 * We get here by default in NSET and FSET states and
		 * from above in FREQ or SPIK states.
		 *
		 * In NSET state an initial frequency correction is not
		 * available, usually because the frequency file has not
		 * yet been written. Since the time is outside the step
		 * threshold, the clock is stepped. The frequency will
		 * be set directly following the stepout interval.
		 *
		 * In FSET state the initial frequency has been set from
		 * the frequency file. Since the time is outside the
		 * step threshold, the clock is stepped immediately,
		 * rather than after the stepout interval. Guys get
		 * nervous if it takes 15 minutes to set the clock for
		 * the first time.
		 *
		 * In FREQ and SPIK states the stepout threshold has
		 * expired and the phase is still above the step
		 * threshold. Note that a single spike greater than the
		 * step threshold is always suppressed, even with a
		 * long time constant.
		 */ 
		default:
			INF("CLOCKRESET: %+.6f", fp_offset);
			step_systime(fp_offset);
//			reinit_timer();
			sync.clock_jitter = sync.clock_precision;
			rval = 2;
			if (sync.state == EVNT_NSET || 
				(sync.current_time - sync.last_step) < CLOCK_MINSTEP * 2) {
				rstclock(EVNT_FREQ, 0);
				return (rval);
			}
			sync.last_step = sync.current_time;
			break;
		}
		rstclock(EVNT_SYNC, 0);
	} else {
		DBG2("fp_offs(%f) <= CLOCK_MAX(%f)", fp_offset, CLOCK_MAX);
		/*
		 * The offset is less than the step threshold. Calculate
		 * the jitter as the exponentially weighted offset
		 * differences.
		 */
		etemp = SQUARE(sync.clock_jitter);
		dtemp = SQUARE(MAX(fabs(fp_offset - sync.last_offset), 
						   sync.clock_precision));

		sync.clock_jitter = SQRT(etemp + (dtemp - etemp) / CLOCK_AVG);

		DBG2("[%s]", __st_nm[sync.state]);
		switch (sync.state) {

			/*
			 * In NSET state this is the first update received and
			 * the frequency has not been initialized. Adjust the
			 * phase, but do not adjust the frequency until after
			 * the stepout threshold.
			 */
		case EVNT_NSET:
			rstclock(EVNT_FREQ, fp_offset);
			break;

			/*
			 * In FREQ state ignore updates until the stepout
			 * threshold. After that, compute the new frequency, but
			 * do not adjust the phase or frequency until the next
			 * update.
			 */
		case EVNT_FREQ:
			if (mu < CLOCK_MINSTEP) {
				INF("FREQ: (mu=%f < CLOCK_MINSTEP=%f)", mu, CLOCK_MINSTEP);
				break;
				return 0;
			}

			clock_frequency = direct_freq(fp_offset);
			rstclock(EVNT_SYNC, 0);
			break;

			/*
			 * We get here by default in SYNC and SPIK states. Here
			 * we compute the frequency update due to PLL and FLL
			 * contributions.
			 */
		default:
			/*
			 * The PLL frequency gain (numerator) depends on
			 * the minimum of the update interval and Allan
			 * intercept. This reduces the PLL gain when the 
			 * FLL becomes effective.
			 */ 
			etemp = MIN(CLOCK_ALLAN, mu);
			dtemp = 4 * CLOCK_PLL * SYS_POLL;
			clock_frequency += fp_offset * etemp / (dtemp * dtemp);
			rstclock(EVNT_SYNC, fp_offset);
			break;
		}
	}


#if 1
	if (sync.state == EVNT_SYNC) {
		int32_t offset;
		int32_t constant;
		int32_t esterror;
		int32_t drift_comp;

		offset = FLOAT_Q31(sync.clock_offset);
		(void)offset;
		(void)drift_comp;
		constant = mu;
		(void)constant;
		esterror = FLOAT_Q31(sync.clock_jitter);
		(void)esterror;

		DBG3("off %.6f,%.6f jit %.6f,%.6f", 
			sync.clock_offset, Q31_FLOAT(offset), 
			sync.clock_jitter, Q31_FLOAT(esterror));

		DBG3("mu=%f constant=%d", mu, constant); 

		drift_comp = pll_phase_adjust(&sync.pll, offset, constant);
		drift_comp = clock_drift_comp(sync.sys_clk, drift_comp);

		clock_frequency = Q31_FLOAT(drift_comp);
		DBG1("freq=%.8f", clock_frequency); 
	}
#endif

	/*
	 * Clamp the frequency within the tolerance range and calculate
	 * the frequency difference since the last update.
	 */
	if (fabs(clock_frequency) > NTP_MAXFREQ) {
		ERR("frequency error %.0f PPM exceeds tolerance %.0f PPM", 
			clock_frequency * 1e6, NTP_MAXFREQ * 1e6);
	}

	dtemp = SQUARE(clock_frequency - sync.drift_comp);
	if (clock_frequency > NTP_MAXFREQ)
		sync.drift_comp = NTP_MAXFREQ;
	else if (clock_frequency < -NTP_MAXFREQ)
		sync.drift_comp = -NTP_MAXFREQ;
	else
		sync.drift_comp = clock_frequency;

	/*
	 * Calculate the wander as the exponentially weighted RMS
	 * frequency differences. Record the change for the frequency
	 * file update.
	 */
	etemp = SQUARE(sync.clock_stability);
	sync.clock_stability = SQRT(etemp + (dtemp - etemp) / CLOCK_AVG);

#if 0
	/*
	 * Here we adjust the timeconstan by comparing the current
	 * offset with the clock jitter. If the offset is less than the
	 * clock jitter times a constant, then the averaging interval is
	 * increased, otherwise it is decreased. A bit of hysteresis
	 * helps calm the dance. Works best using burst mode.
	 */
	if (fabs(ck->clock_offset) < CLOCK_PGATE * ck->clock_jitter) {
		ck->tc_counter += ck->sys_poll;
		if (ck->tc_counter > CLOCK_LIMIT) {
			ck->tc_counter = CLOCK_LIMIT;
			if (ck->sys_poll < peer->maxpoll) {
				ck->tc_counter = 0;
				ck->sys_poll++;
			}
		}
	} else {
		ck->tc_counter -= ck->sys_poll << 1;
		if (ck->tc_counter < -CLOCK_LIMIT) {
			ck->tc_counter = -CLOCK_LIMIT;
			if (ck->sys_poll > peer->minpoll) {
				ck->tc_counter = 0;
				ck->sys_poll--;
			}
		}
	}
#endif

	/*
	 * Yibbidy, yibbbidy, yibbidy; that'h all folks.
	 */
	INF("offs%.6f jit %.6f freq %.1f stab %.3f", 
		sync.clock_offset, sync.clock_jitter, 
		sync.drift_comp * 1e6, sync.clock_stability * 1e6);

	return rval;
}

__thread int off_raw_var;
__thread int off_filt_var;

void synclk_receive(uint64_t local, uint64_t remote)
{
	struct synclk_peer * peer = &sync.peer;
	double offs;
	double disp;
	double delay;

	peer->timestamp = remote;

	offs = LFP2D((int64_t)(remote - local));
	delay = peer->delay;
	offs -= delay;
	disp = sync.clock_precision + peer->precision + CLOCK_PHI * delay;

//	chime_var_rec(off_raw_var, offs);

	if (!synclk_clock_filter(offs, delay, disp))
		return;

//	chime_var_rec(off_filt_var, peer->offset);

	DBG1("clock offset = %.6f", peer->offset);
	synclk_local_clock(peer->offset);

	/*
	 * If this is the first time the clock is set, reset the
	 * leap bits. If crypto, the timer will goose the setup
	 * process.
	 */
	if (sync.sys_leap == LEAP_NOTINSYNC) {
		sync.sys_leap = LEAP_NOWARNING;
	}
}

static void synclk_filter_init(struct synclk_filter * filter)
{
	int i;

	filter->nextpt = 0;

	for (i = 0; i < SYNC_FILTER_LEN; i++) {
		filter->order[i] = i;
		filter->disp[i] = MAXDISPERSE;
	}
}

void synclk_peer_init(struct synclk_peer * peer, 
					  float delay, float precision)
{
	/*
	 * Clear all values, including the optional crypto values above.
	 */
	memset(peer, 0, sizeof(struct synclk_peer));

	peer->disp = MAXDISPERSE;
	peer->jitter = sync.clock_precision;

	peer->timestamp = 0;
	peer->precision = precision;
	peer->delay = delay; /* broadcast delay (s) */
//	peer->bias = -delay / 2.;


	synclk_filter_init(&peer->filter);
}


void synclk_init(struct clock * clk, float peer_delay, float peer_precision)
{
	struct synclk_peer * peer = &sync.peer;
	sync.current_time = 0;
	sync.clock_epoch = 0;
	sync.sys_clk = clk; 
	sync.sys_leap = LEAP_NOTINSYNC;

	sync.clock_precision = LFP2D(clk->resolution);
	sync.clock_jitter = sync.clock_precision;
	sync.clock_stability = 0;
	rstclock(EVNT_NSET, 0);

	synclk_peer_init(peer, peer_delay, peer_precision);
	INF("clock precision = %.6f", sync.clock_precision);
	
	off_raw_var = chime_cpu_var_open("off_raw");
	off_filt_var = chime_cpu_var_open("off_filt");
}


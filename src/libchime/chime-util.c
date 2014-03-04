#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define __CHIME_I__
#include "chime-i.h"
#include "objpool.h"
#include <math.h>

/*****************************************************************************
  Public utility API
 *****************************************************************************/

/* Sleep for specified number of seconds. 
   The difference from POSIX sleep is that it will not
   return if interrupted by a signal. */
void chime_sleep(unsigned int sec)
{       
	__msleep(sec * 1000);
}

/* Similar to sleep() with time in milliseconds*/
void chime_msleep(unsigned int ms)
{       
	__msleep(ms);
}

/* Should be called from the main(). This will
 initialize signals, and signal handlers. 
 Alert: The application cannot use SIGALRM as the interval timer 
 depends on it to work. */
void chime_app_init(void (* on_cleanup)(void))
{       
	__term_sig_handler(on_cleanup);
}

#define DIR_LIST_MAX 127

void __dir_lst_clear(struct dir_lst * lst)
{
	DBG1("lst->cnt=%d.", lst->cnt);
	lst->cnt = 0;
}

uint16_t __dir_lst_lookup(struct dir_lst * lst, const char * name) 
{
	int oid = OID_NULL;
	int i;

	DBG1("cnt=%d name=%s", lst->cnt, name);

	for (i = 0; i < lst->cnt; ++i) {
		oid = lst->entry[i].oid;
		DBG1("%2d - name=\"%s\" OID=%d", i, lst->entry[i].name, oid);
		if (oid == OID_NULL)
			break;
		if (strcmp(name, lst->entry[i].name) == 0)
			break;
		oid = OID_NULL;
	}

	return oid;
}

bool __dir_lst_insert(struct dir_lst * lst, const char * name, int oid) 
{
	int i;

	if ((i = lst->cnt) >= DIR_LIST_MAX)
		return false;

	lst->entry[i].oid = oid;
	strcpy(lst->entry[i].name, name);
	lst->cnt++;

	DBG1("%2d - name=\"%s\" OID=%d", i, lst->entry[i].name, lst->entry[i].oid);

	return true;
}

/*****************************************************************************
 * Random number generators
 *****************************************************************************/

#define U64_RAND_MAX 0xfffffffffffffLL
#define U64_TO_DOUBLE(X) ((double)(X) * (1.0 / (double)(U64_RAND_MAX + 1)))

/* Uniform Distribution Random number generator */
double unif_rand(uint64_t * seedp) 
{
	uint64_t randseed  = *seedp;
	uint64_t val;

	randseed = (randseed * 6364136223846793005LL) + 1LL;
	val = (randseed >> 12) & U64_RAND_MAX;

	*seedp = randseed;

	return U64_TO_DOUBLE(val);
}

/* Normal Distribution Random number generator
   Aproximation of Normal Distribution using Center Limit Theorem. */

#define NORM_CLT_N 16
#define NORM_VARIANCE 1.0 / NORM_CLT_N

double norm_rand(uint64_t * seedp) 
{
	uint64_t randseed  = *seedp;
	uint64_t val = 0;
	int i;

	for (i = 0; i < NORM_CLT_N; ++i) {
		randseed = (randseed * 6364136223846793005LL) + 1LL;
		val += (randseed >> 12) & U64_RAND_MAX;
	}

	*seedp = randseed;
	val /= NORM_CLT_N;

	return U64_TO_DOUBLE(val);
}

/* Exponential Distribution Random number generator */

#define EXP_CLT_N 8
#define EXP_VARIANCE 1.0 / EXP_CLT_N

void exp_rand_init(struct exp_rand_state * e, double z, uint64_t seed)
{
	double xa;
	double xb;
	int i;

	xa = 1.0;
	xb = 1.0;

	for (i = 0; i < EXP_CLT_N; ++i) {
		xa *= z;
		xb *= 1.0 + z;
	}

	e->seed = seed;
	e->x0 = z;
	e->x1 = xa;
	e->scale = 1.0 / (xb - xa);
}

double exp_rand(struct exp_rand_state * e) 
{
	uint64_t randseed  = e->seed;
	double x0 = e->x0;
	double x = 1.0;
	int i;

	for (i = 0; i < EXP_CLT_N; ++i) {
		uint64_t r;

		randseed = (randseed * 6364136223846793005LL) + 1LL;
		r = (randseed >> 12) & U64_RAND_MAX;
		x *= U64_TO_DOUBLE(r) + x0;
	}

	x = (x - e->x1) * e->scale;

	e->seed = randseed;

	return x;
}

/*****************************************************************************
 * Bitmap allocation
 *****************************************************************************/

void __bmp_alloc_init(uint64_t bmp[], int len) 
{
	int j;

	for (j = 0; j < len; ++j)
		bmp[j] = 0;
}

int __bmp_bit_alloc(uint64_t bmp[], int len) 
{
	uint64_t msk;
	int j;
	int i;
	int id;

	for (j = 0; j < len; ++j) {
		if ((msk = bmp[j]) != -1) {
			DBG3("bmp[%d]=%016"PRIx64".", j, msk);
			/* XXX: GCC compatibility */
			i = __builtin_ffsll(~msk) - 1;
			id = (j << 6) + i;
			bmp[j] = msk | (1LL << i);
			return id;
		}
	};

	return -1;
}

int __bmp_bit_free(uint64_t bmp[], int len, int bit) 
{
	int i;
	int j;

	assert(bit >= 0);
	assert(bit < (1 << len));

	/* j = bit >> log2(64); */
	j = bit >> 6;
	i = bit - (j << 6);

	bmp[j] &= ~(1LL << i);

	return 0;
}


/*
   ** find rational approximation to given real number
   ** David Eppstein / UC Irvine / 8 Aug 1993
   **
   ** With corrections from Arno Formella, May 2008
   **
   ** usage: a.out r d
   **   r is real number to approx
   **   d is the maximum denominator allowed
   **
   ** based on the theory of continued fractions
   ** if x = a1 + 1/(a2 + 1/(a3 + 1/(a4 + ...)))
   ** then best approximation is found by truncating this series
   ** (with some adjustments in the last term).
   **
   ** Note the fraction can be recovered as the first column of the matrix
   **  ( a1 1 ) ( a2 1 ) ( a3 1 ) ...
   **  ( 1  0 ) ( 1  0 ) ( 1  0 )
   ** Instead of keeping the sequence of continued fraction terms,
   ** we just keep the last partial product of these matrices.
   */

bool float_to_ratio(struct ratio * r, double x, uint32_t maxden)
{
	int32_t m[2][2];
	double startx;
	int32_t ai;
	double e0;
	double e1;

	startx = x;

	/* initialize matrix */
	m[0][0] = m[1][1] = 1;
	m[0][1] = m[1][0] = 0;

	/* loop finding terms until denom gets too big */
	while (m[1][0] *  (ai = (int32_t)x) + m[1][1] <= maxden) {
		int32_t t;

		t = m[0][0] * ai + m[0][1];
		m[0][1] = m[0][0];
		m[0][0] = t;
		t = m[1][0] * ai + m[1][1];
		m[1][1] = m[1][0];
		m[1][0] = t;

		if (x == (double)ai) 
			break;     /* AF: division by zero */

		x = 1/(x - (double)ai);

		if (x > (double)0x7fffffff) 
			break;  /* AF: representation failure */
	} 
	/* now remaining x is between 0 and 1/ai */
	/* approx as either 0 or 1/m where m is max that will fit in maxden */
	/* first try zero */
	r->p = m[0][0];
	r->q = m[1][0];
	e0 = fabs(startx - ((double) m[0][0] / (double) m[1][0]));
	DBG2("%d/%d, error = %e", m[0][0], m[1][0], e0);

	/* now try other possibility */
	ai = (maxden - m[1][1]) / m[1][0];
	m[0][0] = m[0][0] * ai + m[0][1];
	m[1][0] = m[1][0] * ai + m[1][1];
	e1 = fabs(startx - ((double) m[0][0] / (double) m[1][0]));
	DBG2("%d/%d, error = %e", m[0][0], m[1][0], e1);
	if (e1 < e0) {
		r->p = m[0][0];
		r->q = m[1][0];
	}

	return true;
}




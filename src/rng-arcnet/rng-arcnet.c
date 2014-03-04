#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <limits.h>

#include "debug.h"

#define USEC (1000000000LL)
#define MSEC (1000000000000LL)
#define SEC  (1000000000000000LL)

#define TS2MSEC(X) (uint64_t)((uint64_t)(X)/MSEC)
#define TS2USEC(X) (uint64_t)((uint64_t)(X)/USEC)
#define TS2F(X) ((double)(X)/1e15) 

#define NORM_LOOP_CNT 8

#define CLT_RAND_MAX 0xfffffffffffffLL
/* Random number generator
   Aproximation of Normal Distribution using Center Limit Theorem. */

/* Uniform Distribution Random number generator */
static double unif_rand(uint64_t * seedp) 
{
	uint64_t randseed  = *seedp;
	uint64_t val;

	randseed = (randseed * 6364136223846793005LL) + 1LL;
	val = (randseed >> 12) & CLT_RAND_MAX;

	*seedp = randseed;

	return val / (double)(CLT_RAND_MAX + 1);
}

struct exp_state {
	unsigned int loop_cnt;
	uint64_t seed;
	double x0;
	double x1;
	double scale;
};

void exp_rand_init(struct exp_state * e, double z, double t, uint64_t seed)
{
	unsigned int loop_cnt;
	double xa;
	double xb;
	int i;

	xa = 1.0;
	xb = 1.0;

	loop_cnt = t;

	for (i = 0; i < loop_cnt; ++i) {
		xa *= z;
		xb *= 1.0 + z;
	}

	e->loop_cnt = loop_cnt;
	e->seed = seed;
	e->x0 = z;
	e->x1 = xa;
	e->scale = 1.0 / (xb - xa);
}

static double exp_rand(struct exp_state * e) 
{
	uint64_t randseed  = e->seed;
	double x0 = e->x0;
	double x = 1.0;
	int i;

	for (i = 0; i < e->loop_cnt; ++i) {
		uint64_t r;
		double z;

		randseed = (randseed * 6364136223846793005LL) + 1LL;
		r = (randseed >> 12) & CLT_RAND_MAX;
		z = (r / (double)(CLT_RAND_MAX + 1)) + x0;
		x *= z;
	}

	x = (x - e->x1) * e->scale;
	e->seed = randseed;

//	printf("%0.8f %.8f %0.8f\n", x, xa, xb);


	return x;
}

#define STAT_BINS 256

struct stat_dist {
	uint32_t cnt;
	double sum;
	double bin_scale; /* 1 / (bin interval) */
	double bin_width; /* bin width */
	uint32_t bin[STAT_BINS];
};

void stat_dump(FILE * f, struct stat_dist * stat)
{
	double avg;
	double x_max;
	double x;
	double y_max;
	double y;
	int j;

	avg = stat->sum / stat->cnt;
	printf("- Average = %.6f\n", avg);

	y_max = 0;
	x_max = 0;
	for (j = 0; j < STAT_BINS; ++j) {
		y = (double)stat->bin[j] / (double)stat->cnt;
		x = ((double)j + 0.5) * stat->bin_width;
		if (y > y_max) {
			y_max = y;
			x_max = x;
		}
	}

	printf("- Maximum = %.6f (%.6f)\n", x_max, y_max);
};

bool hist_dump(struct stat_dist * stat, const char * name, 
			   double x_scale, double y_scale)
{
	char fname[256];
	char plt_path[PATH_MAX];
	char dat_path[PATH_MAX];
	char out_path[PATH_MAX];
	double x;
	double y;
	double y_max;
	double x_max;
	double bin_width;
	FILE * f;
	int j;

	strcpy(fname, name);
	sprintf(dat_path, "./%s.dat", fname);
	sprintf(plt_path, "./%s.plt", fname);
	sprintf(out_path, "./%s.png", fname);

	/* Find the maximum values */
	y_max = 0;
	for (j = 0; j < STAT_BINS; ++j) {
		y = ((double)stat->bin[j] / (double)stat->cnt);
		if (y > y_max)
			y_max = y;
	}
	y_max *= y_scale;
	x_max = (stat->bin_width * STAT_BINS * x_scale);
	bin_width = stat->bin_width * x_scale;

	/* Output the data */
	if ((f = fopen(dat_path, "w")) == NULL) {
		ERR("fopen(\"%s\") failed: %s!", dat_path, __strerr());
		return false;
	}
	for (j = 0; j < STAT_BINS; ++j) {
		uint32_t n = stat->bin[j];
		x = j * bin_width;
		y = ((double)n / (double)stat->cnt) * y_scale;
		if (n > 0) {
			fprintf(f, "%10.8f %10.8f\n", x, y / y_max);
		}
	}
	fclose(f);

	/* Output the gnuplot script */
	if ((f = fopen(plt_path, "w")) == NULL) {
		ERR("fopen(\"%s\") failed: %s!", plt_path, __strerr());
		return false;
	}
	fprintf(f, "min=0\n");
	fprintf(f, "max=%.8f\n", x_max);
	fprintf(f, "width=%.8f\n", bin_width);
	fprintf(f, "set terminal png size 1280,768 font 'Verdana,10'\n");
	fprintf(f, "set output '%s'\n", out_path);
	fprintf(f, "set xrange [0:max]\n");
	fprintf(f, "set yrange [0:%.8f]\n", 1.0);
	fprintf(f, "set offset graph 0.05,0.05,0.05,0.0\n");
	fprintf(f, "set boxwidth width\n");
	fprintf(f, "set style fill solid 0.5\n");
	fprintf(f, "set tics out nomirror\n");
	fprintf(f, "set xlabel 'Delay(s)'\n");
	fprintf(f, "set ylabel 'Frequency'\n");
	fprintf(f, "plot '%s' using 1:2 smooth freq "
			"with boxes lc rgb'blue' notitle\n", dat_path);
	fprintf(f, "set output\n");
	fprintf(f, "quit\n");
	fclose(f);

	return true;
}

static void hist_init(struct stat_dist * stat, double max_delay)
{
	int i;

	for (i = 0 ; i < STAT_BINS; ++i) {
		stat->bin[i] = 0;
	}

	stat->bin_width = max_delay / STAT_BINS;
	stat->bin_scale = 1.0 / stat->bin_width;
	stat->cnt = 0;
	stat->sum = 0.0;
}

static inline void hist_add(struct stat_dist * stat, double val)
{
	int n;

	n = val * stat->bin_scale;
	if (n >= STAT_BINS)
		return;
	stat->bin[n]++;
	stat->cnt++;
	stat->sum += val;
}


void arcnet_dist_run(const char * name, double speed, 
				   unsigned int nodes, unsigned int samples)
{
	struct stat_dist stat;
	struct exp_state e;
	uint64_t seed = 10010101823217900LL;
	double bit_time = 1.0 / speed;
	unsigned int i;
	double fix_delay = 500 * bit_time;
	double per_node_delay = 100 * bit_time;
	double max_jitter = 12000 * bit_time;
	double max_delay;
	double t;
	double z;

	t = 8;
	z = 0.3;

	printf("- Combined distribution\n");
	printf("- Z = %f\n", z);

	exp_rand_init(&e, z, t, 9253431413641);

	max_delay = max_jitter + (per_node_delay * nodes) + fix_delay;
	hist_init(&stat, max_delay);

	for (i = 0 ; i < samples; ++i) {
		double jitter;
		double delay;
		double val;

		jitter = exp_rand(&e) * max_jitter;
		delay = unif_rand(&seed) * (per_node_delay * nodes) + fix_delay;
		val = delay + jitter;

		hist_add(&stat, val);
	}

	hist_dump(&stat, name, 1000, 1);
	stat_dump(stdout, &stat);
}


static char * progname;

static void show_usage(void)
{
	fprintf(stderr, "Usage: %s [OPTION...]\n", progname);
	fprintf(stderr, "  -h               Show this help message\n");
	fprintf(stderr, "  -v               Show version\n");
	fprintf(stderr, "  -m <nodes>       Number of nodes\n");
	fprintf(stderr, "  -n <Samples>     Number of samples\n");
	fprintf(stderr, "  -s <Speed>       Network speed (bps)\n");
	fprintf(stderr, "\n");
}

static void show_version(void)
{
	fprintf(stderr, "%s 0.1\n", progname);
	fprintf(stderr, "(C) Copyright 2014, Bob Mittmann\n");
}

int main(int argc, char *argv[]) 
{
	unsigned int samples = 1000000;
	unsigned int nodes = 54;
	double speed = 2500000 / 4;
	int c;

	/* the program name start just after the last slash */
	if ((progname = (char *)strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

	/* parse the command line options */
	while ((c = getopt(argc, argv, "vhm:n:m:s:")) > 0) {
		switch (c) {
		case 'v':
			show_version();
			return 0;
		case 'h':
			show_usage();
			return 0;
		case 'm':
			nodes = strtoul(optarg, NULL, 0);
			break;
		case 'n':
			samples = strtoul(optarg, NULL, 0);
			break;
		case 's':
			speed = strtod(optarg, NULL);
			break;
		default:
			show_usage();
			return 1;
		}
	}

	if (optind != argc) {
		show_usage();
		return 2;
	}

	printf("\n==== ARCnet Probability Distribution ====\n");

	arcnet_dist_run("rng-arcnet", speed, nodes, samples);

	return 0;
}


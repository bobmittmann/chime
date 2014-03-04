#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"

#define SERIAL_COMM 0

struct comm_attr serial_attr = {
	.wr_cyc_per_byte = 1,
	.wr_cyc_overhead = 10,
	.rd_cyc_per_byte = 1,
	.rd_cyc_overhead = 10,
	.bits_overhead = 10,
	.bits_per_byte = 10,
	.bytes_max = 256,
	.speed_bps = 500000, /* 500kbps */
	.jitter = 0, /* seconds */
	.min_delay = 0,  /* minimum delay in seconds */
	.nod_delay = 0,  /* per node delay in seconds */
	.hist_en = false,
	.txbuf_en = false,
	.dcd_en = false
};

__thread uint32_t eot_clk;

void serial_send_msg(uint8_t * msg, int len)
{
	uint32_t clk = chime_cpu_cycles();
	uint32_t dt;

	dt = serial_attr.wr_cyc_overhead;
	dt += serial_attr.wr_cyc_per_byte * len;
	dt += ((double)((serial_attr.bits_per_byte * len) + 
					serial_attr.bits_overhead) 
		   * 1000000) / serial_attr.speed_bps;

	eot_clk = clk + dt;

	DBG1("<%d> clk=%d dt=%d eot=%d", chime_cpu_id(), clk, dt, eot_clk);
	chime_comm_write(SERIAL_COMM, msg, len);
}

void timer1_isr(void)
{
	DBG("<%d> clk=%d", chime_cpu_id(), chime_cpu_cycles());
}

void serial_rcv_isr(void)
{
	static __thread uint32_t clk = 0;
	static __thread uint32_t cnt = 0;
	static __thread uint32_t sum = 0;
	uint8_t buf[256];
	int n;

	chime_tmr_reset(0, 1000000, 0);

	n = chime_comm_read(SERIAL_COMM, buf, 256);
	sum += n * serial_attr.bits_per_byte + serial_attr.bits_overhead;

	if ((++cnt % 1000) == 0) {
		uint32_t cpu_clk = chime_cpu_cycles();
		uint32_t dt = cpu_clk - clk;
		uint32_t speed;

		speed = (sum * 1000) / (dt / 1000);

		DBG("<%d> cpu_clk=%d sum=%d", chime_cpu_id(), cpu_clk, sum);

		tracef(T_INF, "cpu_clk=%d dt=%d spped=%d bps", cpu_clk, dt, speed);
		clk = cpu_clk;
		sum = 0;

	}
}

void serial_eot_isr(void)
{
	uint32_t clk = chime_cpu_cycles();
	uint8_t msg[20] = { 
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	
	if (clk != eot_clk) {
		tracef(T_ERR, "cpu_clk=%d != eot_clk=%d", clk, eot_clk);
		chime_cpu_self_destroy();
	}

	DBG1("<%d> clk=%d", chime_cpu_id(), chime_cpu_cycles());

	serial_send_msg(msg, sizeof(msg));
}

void cpu0_reset(void)
{
	uint8_t msg[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

	/* producer */
	chime_comm_attach(SERIAL_COMM, "serial", NULL, serial_eot_isr, NULL);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	serial_send_msg(msg, sizeof(msg));

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu1_reset(void)
{
	/* consumer */
	chime_comm_attach(SERIAL_COMM, "serial", serial_rcv_isr, NULL, NULL);
	chime_tmr_init(0, timer1_isr, 1000000, 0);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

#define CPU_MAX 4

int main(int argc, char *argv[]) 
{
	int cpu[CPU_MAX];
	int i;

	printf("\n==== Simple communication simulation! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");	
		fflush(stderr);	
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	/* create the serial communication channel */
	if (chime_comm_create("serial", &serial_attr) < 0) {
		DBG1("chime_comm_create() failed!");	
		return 2;
	}

	cpu[0] = chime_cpu_create(0, 0, cpu0_reset);

	for (i = 1; i < CPU_MAX; ++i) {
		cpu[i] = chime_cpu_create(0, 0, cpu1_reset);
	}

	for (i = 0; i < CPU_MAX; ++i) {
		chime_cpu_reset(cpu[i]);
	}

//	chime_reset_all();

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}


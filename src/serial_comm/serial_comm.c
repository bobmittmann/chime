#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"

void timer0_isr(void)
{
	uint8_t msg[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

	INF("<%d> ...", chime_cpu_id());
	chime_comm_write(0, msg, sizeof(msg));
	chime_comm_write(1, msg, sizeof(msg));
	chime_cpu_self_destroy();
}

void timer1_isr(void)
{
	uint8_t msg[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

	INF("<%d> ...", chime_cpu_id());
	chime_comm_write(0, msg, sizeof(msg));
}

void rs485_rcv_isr(void)
{
	uint8_t buf[256];

	INF("<%d> ...", chime_cpu_id());
	chime_comm_read(0, buf, 256);
}

void rs485_eot_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}


void rs232_rcv_isr(void)
{
	uint8_t buf[256];

	INF("<%d> ...", chime_cpu_id());
	chime_comm_read(1, buf, 256);
}

void rs232_eot_isr(void)
{
	INF("<%d> ...", chime_cpu_id());
}

void cpu0_reset(void)
{
	chime_tmr_init(0, timer0_isr, 100000, 1000000);
	chime_tmr_init(1, timer1_isr, 600000, 1000000);
	chime_comm_attach(0, "rs485", rs485_rcv_isr, rs485_eot_isr);
	chime_comm_attach(1, "rs232a", rs232_rcv_isr, rs232_eot_isr);
	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu1_reset(void)
{
	chime_tmr_init(0, timer0_isr, 200000, 1000000);
	chime_tmr_init(1, timer1_isr, 700000, 1000000);
	chime_comm_attach(0, "rs485", rs485_rcv_isr, rs485_eot_isr);
	chime_comm_attach(1, "rs232a", rs232_rcv_isr, rs232_eot_isr);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu2_reset(void)
{
	chime_tmr_init(0, timer0_isr, 300000, 1000000);
	chime_tmr_init(1, timer1_isr, 800000, 1000000);
	chime_comm_attach(0, "rs485", rs485_rcv_isr, rs485_eot_isr);
	chime_comm_attach(1, "rs232a", rs232_rcv_isr, rs232_eot_isr);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu3_reset(void)
{
	chime_tmr_init(0, timer0_isr, 400000, 1000000);
	chime_tmr_init(1, timer1_isr, 800000, 1000000);
	chime_comm_attach(0, "rs485", rs485_rcv_isr, rs485_eot_isr);
	chime_comm_attach(1, "rs232a", rs232_rcv_isr, rs232_eot_isr);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu4_reset(void)
{
	chime_tmr_init(0, timer0_isr,  50000, 1000000);
	chime_tmr_init(1, timer1_isr, 100000, 1000000);
	chime_comm_attach(0, "rs485", rs485_rcv_isr, rs485_eot_isr);
	chime_comm_attach(1, "rs232a", rs232_rcv_isr, rs232_eot_isr);

	tracef(T_INF, "CPU %d started...", chime_cpu_id());

	for (;;) {
		chime_cpu_wait();
	}
}

int main(int argc, char *argv[]) 
{
	struct comm_attr rs485attr = {
		.wr_cyc_per_byte = 10000,
		.wr_cyc_overhead = 20000,
		.rd_cyc_per_byte = 1,
		.rd_cyc_overhead = 1,
		.bits_overhead = 0,
		.bits_per_byte = 10,
		.speed_bps = 9600, /* */
		.jitter = 0.1, /* seconds */
		.min_delay = 0.1,  /* minimum delay in seconds */
		.nod_delay = 0.0,  /* per node delay in seconds */
		.hist_en = false,
		.txbuf_en = false
	};

	struct comm_attr rs232attr = {
		.wr_cyc_per_byte = 10000,
		.wr_cyc_overhead = 20000,
		.rd_cyc_per_byte = 1,
		.rd_cyc_overhead = 1,
		.bits_overhead = 0,
		.bits_per_byte = 10,
		.speed_bps = 9600, /* */
		.jitter = 0.1, /* seconds */
		.min_delay = 0.1,  /* minimum delay in seconds */
		.nod_delay = 0.0,  /* per node delay in seconds */
		.hist_en = false,
		.txbuf_en = false
	};

	printf("\n==== Simple communication simulation! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");	
		fflush(stderr);	
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	/* create the I2C communication channel */
	if (chime_comm_create("rs485", &rs485attr) < 0) {
		DBG1("chime_comm_create() failed!");	
	}

	if (chime_comm_create("rs232a", &rs232attr) < 0) {
		DBG1("chime_comm_create() failed!");	
	}

	chime_cpu_create(0, 0, cpu0_reset);

	chime_cpu_create(1000, 0, cpu1_reset);

	chime_cpu_create(2000, 0, cpu2_reset);

	chime_cpu_create(3000, 0, cpu3_reset);

	chime_cpu_create(4000, 0, cpu4_reset);

	chime_reset_all();

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}


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
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include "chime.h"
#include "console.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 1

/*************************************************************
  ArcNET PAC frame

  Information Symbol Unit (ISU)

  1 1 0 i0 i1 i2 i3 i4 i5 i6 i7 

  ***********************************************************/

/************************************************************
  Arcnet Times

  <ITT>
 ************************************************************/


struct pac_frm {
	uint8_t pac; /* Pac Frame Identifier (1 ISU, value = 0x01) */
	uint8_t sid;
	uint8_t did[2];
	uint8_t il;
	uint8_t sc; /* System Code (1 ISU) */
	uint8_t info_fsc[252 + 2];
} __attribute__((packed))__;

#define ARCNET_COMM 0

/* This ISR is called when data from the RTC is received on the I2c */
void arcnet_rcv_isr(void)
{
	chime_comm_read(ARCNET_COMM, NULL, 0);
}

void cpu_slave(void)
{
	tracef(T_DBG, "Slave reset...");

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", arcnet_rcv_isr, NULL);

	for (;;) {
		chime_cpu_step(1000);
	}
}

void cpu_master(void)
{
	struct pac_frm frm;
	
	tracef(T_DBG, "Master reset...");

	/* ARCnet network */
	chime_comm_attach(ARCNET_COMM, "ARCnet", NULL, NULL);

	frm.pac = 0x01;
	for (;;) {
		chime_cpu_step(100000);
		chime_comm_write(ARCNET_COMM, &frm, 64);
	}
}

void show_help(void)
{
	printf("\nSimulation commands:\n");
	printf("  [1] - speed x1\n");
	printf("  [2] - speed x10\n");
	printf("  [3] - speed x100\n");
	printf("  [4] - speed x1000\n");
	printf("  [5] - speed x10000\n");
	printf("  [6] - speed x60\n");
	printf("  [0] - speed 1/10\n");
	printf("  [-] - speed 1/100\n");
	printf("  [=] - speed 1/10000\n");
	printf("  [h] - help\n");
	printf("  [p] - pause simulation\n");
	printf("  [q] - quit\n");
	printf("  [r] - resume simulation\n");
	printf("  [y] - reset all CPUs and COMMs\n");
	printf("\n");
}

void system_cleanup(void)
{
	console_close();
	chime_client_stop();
	chime_server_stop();
}

int main(int argc, char *argv[]) 
{
	struct trace_entry * trc;
	struct comm_attr attr = {
		.wr_cyc_per_byte = 2,
		.wr_cyc_overhead = 4,
		.rd_cyc_per_byte = 2,
		.rd_cyc_overhead = 4,
		.bits_overhead = 6,
		.bits_per_byte = 11,
		.bytes_max = 256,
		.speed_bps = 625000, /* bits per second */
		.jitter = 0.1, /* seconds */
		.delay = 0.0  /* minimum delay in seconds */
	};
	int c;

	chime_app_init(system_cleanup);

	console_open();

	/* Title */
	printf(" Combo simulation server %d.%d - ", 
		   VERSION_MAJOR, VERSION_MINOR);
	printf("(C) Copyright 2014, Mircom Group.\n");
	fflush(stdout);

	if (chime_server_start("combo") < 0) {
		fprintf(stderr, "chime_server_start() failed!\n");
		fflush(stderr);
		return 1;
	}	

	if (chime_client_start("combo") < 0) {
		fprintf(stderr, "chime_server_start() failed!\n");
		fflush(stderr);
		return 2;
	}	

	/* create an archnet communication simulation */
	if (chime_comm_create("ARCnet", &attr) < 0) {
		fprintf(stderr, "chime_comm_create() failed!\n");
		fflush(stderr);
		return 3;
	}

	if (chime_cpu_create(100, -0.5, cpu_master) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");
		fflush(stderr);
		return 4;
	}

	if (chime_cpu_create(-100, -0.5, cpu_slave) < 0) {
		fprintf(stderr, "chime_cpu_create() failed!\n");
		fflush(stderr);
		return 5;
	}

	chime_reset_all();


	do {
		while ((trc = chime_trace_get()) != NULL) {
			chime_trace_dump(trc);
			chime_trace_free(trc);
		}

		c = readkey();

		if (c < ' ')
			continue;

		switch (c) {

		case '=':
			chime_server_speed_set(0.0001);
			printf("--- Speed x0.0001 ---\n");
			break;

		case '-':
			chime_server_speed_set(0.01);
			printf("--- Speed x0.01 ---\n");
			break;

		case '0':
			chime_server_speed_set(0.1);
			printf("--- Speed x0.1 ---\n");
			break;

		case '1':
			chime_server_speed_set(1.0);
			printf("--- Speed x1 ---\n");
			break;

		case '2':
			chime_server_speed_set(10);
			printf("--- Speed x10 ---\n");
			break;

		case '3':
			chime_server_speed_set(100);
			printf("--- Speed x100 ---\n");
			break;

		case '4':
			chime_server_speed_set(1000);
			printf("--- Speed x1,000 ---\n");
			break;

		case '5':
			chime_server_speed_set(10000);
			printf("--- Speed x10,000 ---\n");
			break;

		case '6':
			chime_server_speed_set(60);
			printf("--- Speed x60 ---\n");
			break;

		case 'i':
			chime_server_info(stdout);
			break;

		case 'h':
			show_help();
			break;

		case 't':
			chime_server_comm_stat();
			break;

		case 'p':
			chime_server_pause();
			printf("--- Pause ---\n");
			break;

		case 'r':
			printf("--- Resume ---\n");
			chime_server_resume();
			break;

		case 'y':
			printf("--- Reset ---\n");
			chime_server_reset();
			break;

		case 'q':
			break;

		default:
			printf("[%c]", c);
		}

		fflush(stdout);
	} while (c != 'q');

	system_cleanup();

	return 0;
}


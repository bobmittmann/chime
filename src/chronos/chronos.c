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

void show_help(void)
{
	console_lock();
	printf("\nChronos simulation keys:\n");
	printf("  [1] - speed x1\n");
	printf("  [2] - speed x10\n");
	printf("  [3] - speed x100\n");
	printf("  [4] - speed x1000\n");
	printf("  [5] - speed x10000\n");
	printf("  [6] - speed x60\n");
	printf("  [0] - speed 1/10\n");
	printf("  [-] - speed 1/100\n");
	printf("  [=] - speed 1/10000\n");
	printf("  [d] - dump statistics and variables\n");
	printf("  [h] - help\n");
	printf("  [p] - pause simulation\n");
	printf("  [q] - quit\n");
	printf("  [r] - resume simulation\n");
	printf("  [v] - dump variables\n");
	printf("  [y] - reset all CPUs and COMMs\n");
	printf("\n");
	console_unlock();
}

void system_cleanup(void)
{
	chime_trace_dump_stop();
	console_close();
	chime_server_stop();
}

int main(int argc, char *argv[]) 
{
	int c;

	chime_app_init(system_cleanup);

	console_open();

	console_lock();
//	clrscr();

	/* Title */
	printf(VT100_ATTR_FG_YELLOW VT100_ATTR_BG_BLUE); 
	printf(" Chronos simulation server %d.%d - ", 
		   VERSION_MAJOR, VERSION_MINOR);
	printf("(C) Copyright 2013, Mircom Group.");
	printf(VT100_CLREOL VT100_ATTR_OFF);

	printf("\n");
	fflush(stdout);

	console_unlock();

	if (chime_server_start("chronos") < 0) {
		fprintf(stderr, "chime_server_start() failed!\n");
		fflush(stderr);
		return 1;
	}	

	chime_trace_dump_start();

	do {

		c = readkey();

		if (c < ' ')
			continue;

		switch (c) {

		case '=':
			printf("--- Speed x0.001 ---\n");
			chime_server_speed_set(0.001);
			break;

		case '-':
			printf("--- Speed x0.01 ---\n");
			chime_server_speed_set(0.01);
			break;

		case '0':
			printf("--- Speed x0.1 ---\n");
			chime_server_speed_set(0.1);
			break;

		case '1':
			printf("--- Speed x1 ---\n");
			chime_server_speed_set(1.0);
			break;

		case '2':
			printf("--- Speed x10 ---\n");
			chime_server_speed_set(10);
			break;

		case '3':
			printf("--- Speed x100 ---\n");
			chime_server_speed_set(100);
			break;

		case '4':
			printf("--- Speed x1,000 ---\n");
			chime_server_speed_set(1000);
			break;

		case '5':
			printf("--- Speed x10,000 ---\n");
			chime_server_speed_set(10000);
			break;

		case '6':
			printf("--- Speed x100,000---\n");
			chime_server_speed_set(100000);
			break;

		case '7':
			printf("--- Speed x1,000,000---\n");
			chime_server_speed_set(1000000);
			break;

		case '8':
			printf("--- Speed x5 ---\n");
			chime_server_speed_set(5);
			break;

		case 'i':
			chime_server_info(stdout);
			break;

		case 'h':
			show_help();
			break;

		case 'd':
			printf("--- Dump ---\n");
			chime_server_comm_stat();
			chime_server_var_dump();
			break;

		case 'v':
			printf("--- Variables ---\n");
			chime_server_var_dump();
			break;

		case 'p':
			printf("--- Pause ---\n");
			chime_server_pause();
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


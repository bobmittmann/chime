#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "chime.h"
#include "debug.h"

#define ARCNET_COMM 0

void cpu0_reset(void)
{
	for (;;) {
		chime_cpu_step(1);
	}
}

void cpu1_reset(void)
{
	for (;;) {
		chime_cpu_step(1);
	}
}

void cpu2_reset(void)
{
	for (;;) {
		chime_cpu_step(1);
	}
}

void cpu3_reset(void)
{
	for (;;) {
		chime_cpu_step(1);
	}
}

int main(int argc, char *argv[]) 
{
	printf("\n==== Step-by-step simulation test ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		ERR("chime_client_start() failed!");	
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	chime_cpu_create(0, 0, cpu0_reset);

	chime_cpu_create(1000, 0, cpu1_reset);

	chime_cpu_create(2000, 0, cpu2_reset);

	chime_cpu_create(3000, 0, cpu3_reset);

	chime_reset_all();

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}


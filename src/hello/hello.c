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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>

#include "debug.h"

int main(int argc, char *argv[]) 
{
	int val = 0;
	int i;

	if (argc > 1)
		val = strtoul(argv[1], NULL, 0);

	i = 1;

	do {
		usleep(250000);

		printf("+");
		fflush(stdout);

		if (i == val) {
			val++;
			printf("?");
			fflush(stdout);
			continue;
		}

		printf("!");
		fflush(stdout);

	} while (i++ < 10);


	return 0;
}


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


#define	CLOCK_PHI FLOAT_CLK(15e-6) /* max frequency error (s/s) */

void filt_init(struct clock_filt * filt, struct clock  * clk)
{
	filt->clk = clk; 
	filt->precision = clk->resolution; 

	filt->peer.precision = 0;
	filt->peer.delay = 0;
}

/* Reset the clock network filter.
   - peer_delay: average network delay (CLK format)
   - peer_precision: precison of this clock (CLK format) */
void filt_reset(struct clock_filt * filt, int32_t peer_delay, 
				int32_t peer_precision)
{
	filt->peer.delay  = peer_delay;
	filt->peer.precision = peer_precision;

	DBG("<%d> delay=%s precision=%s", chime_cpu_id(), 
		FMT_CLK(peer_delay), FMT_CLK(peer_precision));
}

/* Called when a time packed is received from the network */
int64_t filt_receive(struct clock_filt * filt, 
					 uint64_t remote, uint64_t local)
{
	int32_t delay;
	int64_t offs;
	int64_t disp;

	filt->peer.timestamp = remote;

	offs = (int64_t)(remote - local);
	delay = filt->peer.delay;
	offs += delay;
	disp = filt->precision + filt->peer.precision + CLOCK_PHI * delay;
	(void)disp;

	/* FIXME: implement filtering */
	DBG1("remote=%s local=%s offs=%s delay=%s", 
		FMT_CLK(remote), FMT_CLK(local), FMT_CLK(offs), FMT_CLK(delay));

	return offs;
}


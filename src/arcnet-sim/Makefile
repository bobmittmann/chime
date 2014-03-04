# File:		Makefile
# Module: 
# Target:
# Comment:  
# Copyright(C) 2012 Bob Mittmann. All Rights Reserved.
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
# 

include ../scripts/config.mk

PROG = arcnet-sim

CFILES = pkt-pool.c pkt-heap.c fx-arcnet.c arcnet-sim.c 
#backtrace.c

LIBDIRS = ../libchime

LIBS = chime 

ifeq ($(HOST),Cygwin)
LIBS += pthread
endif

ifeq ($(HOST),Linux)
LIBS += rt
endif

ifeq ($(HOST),Msys)
#LIBS += bfd intl iberty imagehlp
endif

ifeq ($(dbg_level),0)
CDEFS = NDEBUG
CFLAGS = -ggdb3 -O0
else
CFLAGS = -ggdb3 -O0
endif

INCPATH = ../include

include ../scripts/prog.mk


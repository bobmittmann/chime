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

PROG = single

CFILES = single.c

LIBDIRS = ../libchime

LIBS = chime pthread rt

ifeq ($(HOST),Linux)
LIBS += rt
endif

ifeq ($(dbg_level),0)
CDEFS = NDEBUG
CFLAGS = -g -O2
else
CFLAGS = -g -O0
endif

INCPATH = ../include

include ../scripts/prog.mk


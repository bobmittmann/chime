/* 
 * File:	 console.h
 * Author:   Robinson Mittmann (bobmittmann@gmail.com)
 * Target:
 * Comment:
 * Copyright(C) 2013 Bob Mittmann. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include "vt100.h"

#define MK_IN_KEY(CODE)      (0x2000 + (CODE))
#define IN_CTRL              0x4000

#define IN_CURSOR_UP         MK_IN_KEY(0)
#define IN_CURSOR_DOWN       MK_IN_KEY(1)
#define IN_CURSOR_RIGHT      MK_IN_KEY(2)
#define IN_CURSOR_LEFT       MK_IN_KEY(3)
#define IN_PAGE_UP           MK_IN_KEY(5)
#define IN_PAGE_DOWN         MK_IN_KEY(6)
#define IN_INSERT            MK_IN_KEY(7)
#define IN_DELETE            MK_IN_KEY(8)
#define IN_HOME              MK_IN_KEY(9)
#define IN_END               MK_IN_KEY(10)

#define IN_CTRL_CURSOR_UP    IN_CURSOR_UP + IN_CTRL 
#define IN_CTRL_CURSOR_DOWN  IN_CURSOR_DOWN + IN_CTRL   
#define IN_CTRL_CURSOR_RIGHT IN_CURSOR_RIGHT + IN_CTRL    
#define IN_CTRL_CURSOR_LEFT  IN_CURSOR_LEFT + IN_CTRL   
#define IN_CTRL_PAGE_UP      IN_PAGE_UP + IN_CTRL   
#define IN_CTRL_PAGE_DOWN    IN_PAGE_DOWN + IN_CTRL   

#ifdef __cplusplus
extern "C" {
#endif

int readkey(void);

int console_open(void);

void console_close(void);

void clrscr(void);

void console_lock(void);

void console_unlock(void);

#ifdef __cplusplus
}
#endif	

#endif /* __CONSOLE_H__ */


/* 
 * File:	 vt100.h
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

#ifndef __VT100_H__
#define __VT100_H__

#define ESC "\033"

#define VT100_GOTO ESC "[%d;%df"

#define VT100_HOME ESC "[;H"

#define VT100_CLRSCR ESC "[2J"

#define VT100_CLREOL ESC "[K"

#define VT100_CURSOR_SHOW ESC "[?25h"

#define VT100_CURSOR_HIDE ESC "[?25l"

#define VT100_CURSOR_SAVE ESC "[s"

#define VT100_CURSOR_UNSAVE ESC "[u"

#define VT100_ATTR_SAVE ESC "[7"

#define VT100_ATTR_RESTORE ESC "[8"

#define VT100_ATTR_OFF ESC "[0m"

#define VT100_ATTR_BRIGHT ESC "[1m"

#define VT100_ATTR_DIM ESC "[2m"

#define VT100_ATTR_UNDERSCORE ESC "[4m"

#define VT100_ATTR_BLINK ESC "[5m"

#define VT100_ATTR_REVERSE ESC "[7m"

#define VT100_ATTR_HIDDEN ESC "[8m"

#define VT100_ATTR_FG_BLACK   ESC "[30m"
#define VT100_ATTR_FG_RED     ESC "[31m"
#define VT100_ATTR_FG_GREEN   ESC "[32m"
#define VT100_ATTR_FG_YELLOW  ESC "[33m"
#define VT100_ATTR_FG_BLUE    ESC "[34m"
#define VT100_ATTR_FG_MAGENTA ESC "[35m"
#define VT100_ATTR_FG_CYAN    ESC "[36m"
#define VT100_ATTR_FG_WHITE   ESC "[37m"

#define VT100_ATTR_BG_BLACK   ESC "[40m"
#define VT100_ATTR_BG_RED     ESC "[41m"
#define VT100_ATTR_BG_GREEN   ESC "[42m"
#define VT100_ATTR_BG_YELLOW  ESC "[43m"
#define VT100_ATTR_BG_BLUE    ESC "[44m"
#define VT100_ATTR_BG_MAGENTA ESC "[45m"
#define VT100_ATTR_BG_CYAN    ESC "[46m"
#define VT100_ATTR_BG_WHITE   ESC "[47m"

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif	

#endif /* __VT100_H__ */


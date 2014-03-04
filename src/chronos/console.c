#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "console.h"

#include "debug.h"

#if defined(_WIN32) 
#include <windows.h>
#else
#include <termios.h>
#endif

static struct {
	unsigned int open;
#if defined(_WIN32) 
	bool win_console;
	HANDLE hStdin; 
	DWORD fdwSaveOldMode;
#else
	pthread_mutex_t mutex;
	struct termios settings;
#endif
} console = {
	.open = 0,
#if defined(_WIN32) 
#else
	.mutex = PTHREAD_MUTEX_INITIALIZER
#endif
};

int console_open(void)
{
	int ret = -1;

//	pthread_mutex_lock(&console.mutex); 

	while (console.open == 0) {
#if defined(_WIN32) 
		DWORD fdwMode; 

		console.hStdin = GetStdHandle(STD_INPUT_HANDLE); 
		if (console.hStdin == INVALID_HANDLE_VALUE) {
			ERR("GetStdHandle(STD_INPUT_HANDLE) failed!");
			break;
		}

		// Save the current input mode, to be restored on exit. 
		if (!GetConsoleMode(console.hStdin, &console.fdwSaveOldMode)) {
			DBG("GetConsoleMode() failed!");
			// Get the standard input handle. 
			console.win_console = false;
			printf(VT100_CURSOR_HIDE);
			fflush(stdout);
		} else {
			// Enable the window and mouse input events. 
			fdwMode = console.fdwSaveOldMode;
			fdwMode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | 
						 ENABLE_ECHO_INPUT); 
			if (!SetConsoleMode(console.hStdin, fdwMode)) {
				ERR("SetConsoleMode() failed!");
				break;
			}
			console.win_console = true;
		}
#else
		struct termios new_settings;

		if (tcgetattr(STDIN_FILENO, &console.settings) < 0
			|| !(console.settings.c_lflag & ECHO)) {
			break;
		}

		new_settings = console.settings;
		/* ~ICANON: unbuffered input (most c_cc[] are disabled, VMIN/VTIME are enabled) */
		/* ~ECHO, ~ECHONL: turn off echoing, including newline echoing */
		/* ~ISIG: turn off INTR (ctrl-C), QUIT, SUSP */
		//	new_settings.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG);
		new_settings.c_lflag &= ~(ICANON | ECHO | ECHONL);
		/* reads would block only if < 1 char is available */
		new_settings.c_cc[VMIN] = 1;
		/* no timeout (reads block forever) */
		new_settings.c_cc[VTIME] = 0;
		/* Should be not needed if ISIG is off: */
		/* Turn off CTRL-C */
		/* new_settings.c_cc[VINTR] = _POSIX_VDISABLE; */
		tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);
		printf(VT100_CURSOR_HIDE);
		fflush(stdout);
#endif

		ret = 0;
		console.open++;
	}

//	pthread_mutex_unlock(&console.mutex); 

	return ret;
}

void console_close(void)
{
//	pthread_mutex_lock(&console.mutex); 

	if (console.open > 0) {

		console.open--;

		if (console.open == 0) {
			/* restore console settings */
#if defined(_WIN32) 
			if (console.win_console) {
				SetConsoleMode(console.hStdin, console.fdwSaveOldMode);
			} else {
				printf(VT100_CURSOR_SHOW "\n");
				fflush(stdout);
			}
#else
			printf(VT100_CURSOR_SHOW "\n");
			fflush(stdout);
			tcsetattr(STDIN_FILENO, TCSANOW, &console.settings);
#endif
		}
	}

//	pthread_mutex_unlock(&console.mutex); 
}

void clrscr(void)
{
	printf(VT100_CLRSCR VT100_HOME); 
	fflush(stdout);
}

void console_lock(void)
{
//	pthread_mutex_lock(&console.mutex); 
}

void console_unlock(void)
{
//	pthread_mutex_unlock(&console.mutex); 
}

#define IN_ESC      '\033'

#define MODE_ESC 1
#define MODE_ESC_VAL1 2
#define MODE_ESC_VAL2 3
#define MODE_ESC_O 4

int readkey(void)
{
	int mode;
	int val;
	int ctrl;
	int c;
	char buf[1];

	mode = 0;
	val = 0;
	ctrl = 0;
	for (;;) {
#if defined(_WIN32) 
		DWORD cNumRead;

		if (console.win_console) {
			if (!ReadConsole(console.hStdin, buf, 1, &cNumRead, NULL)) {
				//			WARN("ReadConsole() failed!");
				return -1;
			}
		} else {
			if (!ReadFile(console.hStdin, buf, 1, &cNumRead, NULL)) {
				WARN("ReadFile() failed!");
				return -1;
			}
		}
#else
		if (read(STDIN_FILENO, buf, 1) != 1) {
			return -1;
		}	
#endif

		c = *buf;

		switch (mode) {
		case MODE_ESC:
			switch (c) {
			case '[':
				mode = MODE_ESC_VAL1;
				val = 0;
				ctrl = 0;
				break;
			case 'O':
				mode = MODE_ESC_O;
				break;
			default:
				mode = 0;
			};
			continue;

		case MODE_ESC_VAL1:
		case MODE_ESC_VAL2:
			switch (c) {
			case '0'...'9':
				val = val * 10 + c - '0';
				continue;
			case 'A':
				/* cursor up */
				c = IN_CURSOR_UP + ctrl;
				break;
			case 'B':
				/* cursor down */
				c = IN_CURSOR_DOWN + ctrl;
				break;
			case 'C':
				/* cursor right */
				c = IN_CURSOR_RIGHT + ctrl;
				break;
			case 'D':
				/* cursor left */
				c = IN_CURSOR_LEFT + ctrl;
				break;
			case '~':
				switch (val) {
				case 1:
					c = IN_HOME + ctrl;
					break;
				case 2:
					c = IN_INSERT + ctrl;
					break;
				case 3:
					/* delete */
					c = IN_DELETE + ctrl;
					break;
				case 5:
					c = IN_PAGE_UP + ctrl;
					break;
				case 6:
					c = IN_PAGE_DOWN + ctrl;
					break;
				default:
					mode = 0;
					continue;
				}
				break;
			case ';':
				mode = MODE_ESC_VAL2;
				ctrl = IN_CTRL;
				val = 0;
				continue;
			default:
				mode = 0;
				continue;
			};
			mode = 0;
			break;

		case MODE_ESC_O:
			switch (c) {
			case 'F':
				/* end */
				c = IN_END;
				break;
			case 'H':
				/* end */
				c = IN_HOME;
				break;
			default:
				mode = 0;
				continue;
			}
			mode = 0;
			break;

		default:
			if (c == IN_ESC)  {
				mode = MODE_ESC;
				continue;
			}
		} 

		return c;


	}
}


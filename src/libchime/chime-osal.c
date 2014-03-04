#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define __CHIME_I__
#include "chime-i.h"

#include "debug.h"

/*****************************************************************************
  OSAL: Operating System Abstraction Layer

  NOTICE: The functions provided here are helpers to abstract some 
    operating system (OS) calls. 
   They were tested on CygWin and MinGW.
 *****************************************************************************/

/* Sleep for specified number of milliseconds. 
   Unlike the POSIX sleep/usleep it will not
   return if interrupted by a signal. */
void __msleep(unsigned int ms)
{       
#ifdef _WIN32
	Sleep(ms);
#else
	struct timespec tv;

	tv.tv_sec = ms / 1000;
	tv.tv_nsec = ms % 1000;

	while (nanosleep(&tv, &tv)) {
		if (errno != EINTR)
			break;
	}
#endif
}


/*****************************************************************************
 * Application initialization and signal handling
 *****************************************************************************/

#ifndef _WIN32
/* global cleanup callback */
static void (* __term_handler)(void) = NULL;

static void __abort_handler(int signum)
{
	const char msg[] = "\n!!! ABORTED !!!\n";
	int ret = write(STDERR_FILENO, msg, strlen(msg));
	(void)ret;
	_exit(4);
}

static void __termination_handler(int signum)
{
	struct sigaction new_action;

	DBG1("sig=%d", signum);

	/* Redirect the signal handlers to the abort handler */
	new_action.sa_handler = __abort_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGTERM, &new_action, NULL);
	sigaction(SIGQUIT, &new_action, NULL);

	if (__term_handler != NULL) {
		DBG4("calling custom termination callback");
		__term_handler();
	} else
		WARN("__term_handler==NULLd");

	exit(3);
}
#endif

/* Should be called from the main(). This will
 initialize signals, and signal handlers. 

 Alert: The application cannot use SIGALRM as the interval timer 
 depends on it to work.
 
 */

void __term_sig_handler(void (* handler)(void))
{       
#ifndef _WIN32
	sigset_t set;
	struct sigaction new_action;

	/* Register a cleanup callback routine */
	__term_handler = handler;

	/* Disable SIGALRM signal wich is used by the interval timer. 
	   Only one thread can have this signal enabled. */
	sigemptyset(&set);
//	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	/* Configure the common termination handlers to call
	   the cleanup routine.  */
	new_action.sa_flags = SA_NODEFER;
	new_action.sa_handler = __termination_handler;
	sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGTERM, &new_action, NULL);
	sigaction(SIGQUIT, &new_action, NULL);
#endif
}


/*****************************************************************************
 * Signal handler helpers
 *****************************************************************************/

/* 
   Helper to configure signals that must be blocked.
  */
void __thread_init(const char * name)
{       
#ifndef _WIN32
	sigset_t set;

	sigemptyset(&set);
	/* these signals should be delivered to the main thread only */
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGHUP);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGQUIT);
	/* SIGALRM should be delivered to a single thread handling the 
	 interval timer. */
	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
#endif
}

/*****************************************************************************
 * Message queues
 *****************************************************************************/

int __mq_create(__mq_t * qp, const char * name, 
							  unsigned int maxmsg)
{
	char path[128];
	__mq_t mq;
	int ret;

#ifdef _WIN32
	sprintf(path, "\\\\.\\mailslot\\%s", name);

//	fprintf(stderr, "%s: path=\"%s\"\n", __func__, path);
//	fflush(stderr);

	mq = CreateMailslot(path, 
						maxmsg,                // no maximum message size 
						MAILSLOT_WAIT_FOREVER, // no time-out for operations 
						(LPSECURITY_ATTRIBUTES) NULL); // default security
	ret = (mq == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	struct mq_attr attr = {
		.mq_flags = 0,    /* Flags: 0 or O_NONBLOCK */
		.mq_maxmsg = 2,   /* Max. # of messages on queue */
		.mq_msgsize = maxmsg, /* Max. message size (bytes) */
		.mq_curmsgs = 0   /* # of messages currently in queue */
	};

	sprintf(path, "/%s", name);
	/* create a new message queue */
	mq = mq_open(path, O_RDONLY | O_CREAT, 
				 S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH, &attr);
	ret = (mq == (mqd_t)-1) ? -1 : 0;
#endif
	*qp = mq;
	return ret;
}

int __mq_open(__mq_t * qp, const char * name)
{
	char path[128];
	__mq_t mq;
	int ret;
#ifdef _WIN32
	sprintf(path, "\\\\.\\mailslot\\%s", name);
	
//	fprintf(stderr, "%s: path=\"%s\"\n", __func__, path);
//	fflush(stderr);

	mq = CreateFile(path, 
					GENERIC_WRITE, 
					FILE_SHARE_READ,
					(LPSECURITY_ATTRIBUTES) NULL, 
					OPEN_EXISTING, 
					FILE_ATTRIBUTE_NORMAL, 
					(HANDLE) NULL); 
	ret = (mq == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	sprintf(path, "/%s", name);
	mq = mq_open(path, O_WRONLY);
	ret = (mq == (mqd_t)-1) ? -1 : 0;
#endif
	*qp = mq;
	return ret;
}

void __mq_close(__mq_t mq)
{
#ifdef _WIN32
	CloseHandle(mq);
#else
	mq_close(mq);
#endif
}

void __mq_unlink(const char * name)
{
	char path[128];

#ifdef _WIN32
	sprintf(path, "\\\\.\\mailslot\\%s", name);
#else
	sprintf(path, "/%s", name);
	/* remove existing file */
	mq_unlink(path);
#endif
}

#ifdef _WIN32
void DisplayError(TCHAR* pszAPI, DWORD dwError)
{
	LPVOID lpvMessageBuffer;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
				  FORMAT_MESSAGE_FROM_SYSTEM |
				  FORMAT_MESSAGE_IGNORE_INSERTS,
				  NULL, dwError,
				  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				  (LPTSTR)&lpvMessageBuffer, 0, NULL);

	//... now display this string
	printf(TEXT("ERROR: API        = %s\n"), pszAPI);
	printf(TEXT("       error code = %d\n"), (int)dwError);
	printf(TEXT("       message    = %s\n"), (char *)lpvMessageBuffer);

	// Free the buffer allocated by the system
	LocalFree(lpvMessageBuffer);

//	ExitProcess(GetLastError());
}
#endif

const char * __strerr(void)
{
#ifdef _WIN32
	static char errmsg[128];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
				  FORMAT_MESSAGE_IGNORE_INSERTS,
				  NULL, GetLastError(),
				  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				  (LPTSTR)errmsg, 0, NULL);
	return errmsg;
#else
	return strerror(errno);
#endif
}

/*****************************************************************************
 * Shared memory 
 *****************************************************************************/

int __shm_create(__shm_t * pshm, const char * name, size_t size)
{
	__shm_t shm;
	int ret;
	char path[128];

#ifdef _WIN32
	sprintf(path, "Local\\%s", name);

//	fprintf(stderr, "%s: path=\"%s\" size=%d\n", __func__, path, (int)size);
//	fflush(stderr);

	shm = CreateFileMapping(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security
		PAGE_READWRITE | SEC_COMMIT,          // read/write access
		0,                       // maximum object size (high-order DWORD)
		size,                	// maximum object size (low-order DWORD)
		path);                 // name of mapping object

	ret = (shm == NULL) ? -1 : 0;

//	DisplayError(TEXT("CreateFileMapping"), GetLastError());

#else
	sprintf(path, "/%s", name);
	/* create a new message queue */
	shm = shm_open(path, O_RDWR | O_CREAT | O_EXCL, 
				   S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

	if (shm < 0) {
		ret = shm;
	} else {
		ret = ftruncate(shm, size);
	}

#endif
	*pshm = shm;

	return ret;
}

int __shm_open(__shm_t * pshm, const char * name)
{
	char path[128];
	__shm_t shm;
	int ret;

#ifdef _WIN32
//	sprintf(path, "Global\\%s", name);
	sprintf(path, "Local\\%s", name);
	
//	fprintf(stderr, "%s: path=\"%s\"\n", __func__, path);
//	fflush(stderr);

	shm = OpenFileMapping(
				   FILE_MAP_ALL_ACCESS,   // read/write access
				   FALSE,                 // do not inherit the name
				   path);               // name of mapping object

	ret = (shm == NULL) ? -1 : 0;
#else
	sprintf(path, "/%s", name);
//	shm = shm_open(path, O_RDWR | O_EXCL, 0);
	shm = shm_open(path, O_RDWR, 0);

	ret = shm;
#endif
	*pshm = shm;

	return ret;
}

void * __shm_mmap(__shm_t shm)
{
	void * ptr;

#ifdef _WIN32

//	fprintf(stderr, "%s: shm=%p\n", __func__, shm);
//	fflush(stderr);

	ptr = (LPTSTR) MapViewOfFile(shm, 
								 FILE_MAP_ALL_ACCESS,  // read/write permission
								 0, 0, 0);
#else
	struct stat sb;

	if (fstat(shm, &sb) < 0) {
		return NULL;
	}

//	fprintf(stderr, "%s: size=%d\n", __func__, (int)sb.st_size);
//	fflush(stderr);

	ptr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);

	if (ptr == (void *)-1)
		ptr = NULL;
#endif
	return ptr;
}

int __shm_munmap(__shm_t shm, void * ptr)
{
	int ret;
#ifdef _WIN32
	ret = UnmapViewOfFile(ptr) ? 0 : -1;
#else
	struct stat sb;

	fstat(shm, &sb);

	ret = munmap(ptr, sb.st_size);
#endif
	return ret;
}

void __shm_close(__shm_t shm)
{
#ifdef _WIN32
	CloseHandle(shm);
#else
	close(shm);
#endif
}

void __shm_unlink(const char * name)
{
	char path[128];

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
#else
	sprintf(path, "/%s", name);
	/* remove existing file */
	shm_unlink(path);
#endif
}

/*****************************************************************************
 * Mutex
 *****************************************************************************/

int __mutex_create(__mutex_t * pmtx, const char * name)
{
	char path[128];
	__mutex_t mtx;
	int ret;

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
	/* create a new global mutex. */
	mtx = CreateMutex(NULL, FALSE, path);
	ret = (mtx == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	sprintf(path, "/%s", name);
	/* create a new semaphore with initial value = 1. */
	mtx = sem_open(path, O_RDWR | O_CREAT, 
				   S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH, 1);
	ret = (mtx == (__mutex_t)-1) ? -1 : 0;
#endif
	*pmtx = mtx;

	return ret;
}

int __mutex_open(__mutex_t * pmtx, const char * name)
{
	char path[128];
	__mutex_t mtx;
	int ret;

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
	mtx = OpenMutex(SYNCHRONIZE,   // synchronize access
				   FALSE,          // do not inherit the name
				   path);          // name of mapping object
	ret = (mtx == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	sprintf(path, "/%s", name);
	mtx = sem_open(path, O_RDWR | O_EXCL);
	ret = (mtx == (__mutex_t)-1) ? -1 : 0;
#endif
	*pmtx = mtx;

	return ret;
}

int __mutex_init(__mutex_t * pmtx)
{
	__mutex_t mtx;
	int ret;

#ifdef _WIN32
	/* create an unamed mutex. */
	mtx = CreateMutex(NULL, FALSE, NULL);
	ret = (mtx == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	assert(psem != NULL);	
	mtx = malloc(sizeof(sem_t));
	/* create a new semaphore with initial value = 1. */
	ret = sem_init(mtx, 0, 1);
#endif
	*pmtx = mtx;

	return ret;
}

int __mutex_lock(__mutex_t mtx)
{
	int ret;
#ifdef _WIN32
	if (WaitForSingleObject(mtx, INFINITE) != WAIT_OBJECT_0) {
		fprintf(stderr, "WaitForSingleObject(%p) failed!\n", mtx);
		fflush(stderr);
		ret = -1;
	} else {
		ret = 0;
	}
#else
	ret = sem_wait(mtx);
#endif
	return ret;
}

int __mutex_unlock(__mutex_t mtx)
{
	int ret;
#ifdef _WIN32
	ret = ReleaseMutex(mtx) ? 0 : -1;
#else
	ret = sem_post(mtx);
#endif
	return ret;
}

void __mutex_close(__mutex_t mtx)
{
#ifdef _WIN32
	CloseHandle(mtx);
#else
	sem_close(mtx);
#endif
}

void __mutex_unlink(const char * name)
{
	char path[128];

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
#else
	sprintf(path, "/%s", name);
	/* remove existing file */
	sem_unlink(path);
#endif
}

/*****************************************************************************
 * Shared semaphores
 *****************************************************************************/

int __sem_create(__sem_t * psem, const char * name, unsigned int value)
{
	char path[128];
	__sem_t sem;
	int ret;

#ifdef _WIN32

	sprintf(path, "Global\\%s", name);
	/* create a new global semaphore. */
	sem = CreateSemaphore(NULL, value, LONG_MAX, path);
	ret = (sem == INVALID_HANDLE_VALUE) ? -1 : 0;

#else
	sprintf(path, "/%s", name);
	/* create a new semaphore */
	sem = sem_open(path, O_RDWR | O_CREAT, 
				   S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH, value);
	ret = (sem == SEM_FAILED) ? -1 : 0;
#endif
	*psem = sem;

	return ret;
}

void __sem_unlink(const char * name)
{
	char path[128];

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
#else
	sprintf(path, "/%s", name);
	/* remove existing file */
	sem_unlink(path);
#endif
}

int __sem_open(__sem_t * psem, const char * name)
{
	char path[128];
	__sem_t sem;
	int ret;

#ifdef _WIN32
	sprintf(path, "Global\\%s", name);
	sem = OpenMutex(SYNCHRONIZE,   // synchronize access
				   FALSE,          // do not inherit the name
				   path);          // name of mapping object
	ret = (sem == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	sprintf(path, "/%s", name);
	sem = sem_open(path, O_RDWR | O_EXCL);
	ret = (sem == SEM_FAILED) ? -1 : 0;
#endif
	*psem = sem;

	return ret;
}

int __sem_init(__sem_t * psem, int pshared, unsigned int value)
{
	int ret;
	__sem_t sem;

#ifdef _WIN32
	/* create a new global sem. */
	sem = CreateSemaphore(NULL, value, LONG_MAX, NULL);
	ret = (sem == INVALID_HANDLE_VALUE) ? -1 : 0;
#else
	assert(psem != NULL);	
	sem = malloc(sizeof(sem_t));
	/* create a new semaphore with initial value = 1. */
	ret = sem_init(sem, pshared, value);
#endif
	*psem = sem;

	return ret;
}

void __sem_close(__sem_t sem)
{
#ifdef _WIN32
	CloseHandle(sem);
#else
	sem_close(sem);
#endif
}

int __sem_wait(__sem_t sem)
{
	int ret;
#ifdef _WIN32
	if (WaitForSingleObject(sem, INFINITE) != WAIT_OBJECT_0) {
		fprintf(stderr, "WaitForSingleObject(%p) failed!\n", sem);
		fflush(stderr);
		ret = -1;
	} else {
		ret = 0;
	}
#else
	ret = sem_wait(sem);
#endif
	return ret;
}

int __sem_post(__sem_t sem)
{
	int ret;
#ifdef _WIN32
	ret = ReleaseMutex(sem) ? 0 : -1;
#else
	ret = sem_post(sem);
#endif
	return ret;
}

/*****************************************************************************
 * System interval timer
 *****************************************************************************/

static struct {
	void (* isr)(void);
#ifdef _WIN32
	HANDLE hTimerQueue;
	HANDLE hEvent;
	HANDLE hTimer;
#endif
	bool running;
} __itmr = {
#ifdef _WIN32
	.hTimerQueue = NULL,
	.hEvent = NULL,
	.hTimer = NULL
#else
	.running = false,
#endif

};

#ifdef _WIN32
static VOID CALLBACK __tmr_isr(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	/* second update, signal the pps event */
//	SetEvent(__itmr->hEvent);
	__itmr.isr();
}
#else
static void __alrm_handler(int signum)
{
	__itmr.isr();
}
#endif

int __itmr_init(uint32_t interval_ms, void (* isr)(void))
{
	__itmr.isr = isr;

#ifdef _WIN32
	if (__itmr.hEvent == NULL) {
		/* Use an event object to track the TimerRoutine execution */
		__itmr.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (__itmr.hEvent == NULL) {
			fprintf(stderr, "CreateEvent() failed!\n");
			fflush(stderr);
			return -1;
		}

		/* Create the timer queue. */
		__itmr.hTimerQueue = CreateTimerQueue();
		if (__itmr.hTimerQueue == NULL) {
			fprintf(stderr, "CreateTimerQueue() failed!\n");
			fflush(stderr);
			return -1;
		} 

		/* Create the timer */
		if (!CreateTimerQueueTimer(&__itmr.hTimer, __itmr.hTimerQueue, 
								   (WAITORTIMERCALLBACK)__tmr_isr, 
								   &__itmr, interval_ms, interval_ms, 0)) {
			fprintf(stderr, "CreateTimerQueueTimer() failed!\n");
			fflush(stderr);
			return -1;
		}
	} else {
		/* destroy previous timer */
		if (!ChangeTimerQueueTimer(__itmr.hTimerQueue, __itmr.hTimer, 
								   interval_ms, interval_ms)) {
			fprintf(stderr, "ChangeTimerQueueTimer() failed!\n");
			fflush(stderr);
			return -1;
		}

	}

#else
	struct sigaction new_action;
	struct itimerval it;
	uint32_t sec;
	uint32_t usec;
	sigset_t set;

//	fprintf(stderr, "%s: interval=%u ms.\n", __func__, interval_ms);
//	fflush(stderr);

	if (!__itmr.running) {

//		fprintf(stderr, "%s: initializing interval timer.\n", __func__);
//		fflush(stderr);

		/* set SIGALRM handler */
		new_action.sa_flags = SA_NODEFER;
		new_action.sa_handler = __alrm_handler;
		sigaction(SIGALRM, &new_action, NULL);

		/* unblocks SIGALRM */
		sigemptyset(&set);
		sigaddset(&set, SIGALRM);
		pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	}

	sec = interval_ms / 1000;
	usec = (interval_ms - (sec * 1000)) * 1000;

	it.it_interval.tv_sec = sec;
	it.it_interval.tv_usec = usec;
	it.it_value = it.it_interval;

	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fprintf(stderr, "%s: setitimer() failed: %s.\n",
				__func__, strerror(errno));
		fflush(stderr);
	}

	__itmr.running = true;
#endif
	return 0;
}


int __itmr_stop(void)
{
#ifdef _WIN32
	if (__itmr.hEvent == NULL)
		return -1;

#else

	struct itimerval it;

	if (!__itmr.running)
		return -1;


	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value = it.it_interval;

	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fprintf(stderr, "%s: setitimer() failed: %s.\n",
				__func__, strerror(errno));
		fflush(stderr);
	}

	__itmr.running = false;
#endif

	return 0;
}

/*****************************************************************************
 * Threads
 *****************************************************************************/

int __thread_create(__thread_t * pthread, void *(* task)(void*), void * arg)
{
	__thread_t thread;
	int ret;

#ifdef _WIN32
	unsigned threadId;
	unsigned ( __stdcall *func)(void *);
	
	func = (unsigned (__stdcall *)(void *))task;

	thread = (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, &threadId);
	ret = (thread == (HANDLE)-1L) ? -1 : 0;
#else
	assert(pthread != NULL);	
	thread = malloc(sizeof(pthread_t));

	if ((ret = pthread_create(thread, NULL,
							  (void * (*)(void *))task,
							  (void *)arg)) < 0) {
		fprintf(stderr, "%s: pthread_create() failed: %s.\n",
				__func__, strerror(ret));
		fflush(stderr);
	}
#endif

	*pthread = thread;

	return ret;
}

__thread_t __thread_self(void)
{
#ifdef _WIN32
	/* FIXME: */
	return 0;
#else
	return pthread_self();
#endif
}

int __thread_cancel(__thread_t thread)
{
#ifdef _WIN32
	TerminateThread(thread, 0);
	return 0;
#else
	return pthread_cancel(thread);
#endif
}

int __thread_join(__thread_t thread, void ** value_ptr)
{
#ifdef _WIN32
	WaitForSingleObject(thread, INFINITE);
	return 0;
#else
	return pthread_join(thread, value_ptr);
#endif
}


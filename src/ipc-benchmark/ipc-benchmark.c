#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "debug.h"

#define __CHIME_I__
#include "chime-i.h"

#define MSG_EVENT 0
#define MSG_START 1
#define MSG_SYNC  2
#define MSG_STOP  3

#define MSG_SIZE 8

#ifndef ENABLE_SERVER_THREAD
#define ENABLE_SERVER_THREAD 1
#endif

void print_stats(struct timeval * start, struct timeval * end, 
				 int count, int msg_size)
{
	unsigned int size;
	double dt;

	dt = (double)(end->tv_sec - start->tv_sec) + 
		((double)(end->tv_usec - start->tv_usec) / 1000000.0);

	size = count * msg_size;

	printf("   %7.2f secs, %7.1f Msgs/s, %7.1f KiB/s\n", dt, 
		   count / dt, (size / dt) / 1024);
	fflush(stdout);
}

/* ---------------------------------------------------------------------------
   Message queue
   -------------------------------------------------------------------------- */

const char * mq_req_name = "mq_request";
const char * mq_rep_name = "mq_reply";

int mq_client(int cnt)
{
	struct timeval start;
	struct timeval end;
	__mq_t mq_req;
	__mq_t mq_rep;
	uint32_t msg[MSG_SIZE / 2 + 1];
	int i;

	printf(" - Message queue client...\n");
	fflush(stdout);

	INF("creating reply message queue ...");
	__mq_unlink(mq_rep_name);
	if (__mq_create(&mq_rep, mq_rep_name, MSG_SIZE) < 0) {
		ERR("__mq_create(\"%s\") failed: %s.", mq_rep_name, __strerr());
		return -1;
	}

	INF("Openning request message queue...");
	if (__mq_open(&mq_req, mq_req_name) < 0) {
		ERR("__mq_open(\"%s\") failed: %s.", mq_req_name, __strerr());
		return -1;
	}

	msg[0] = MSG_START;
	msg[1] = 0;

	INF("Start handshake...");
	if (__mq_send(mq_req, msg, MSG_SIZE) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		return -1;
	}

	if (__mq_recv(mq_rep, msg, MSG_SIZE) < 0) {
		ERR("__mq_recv() failed: %s.", __strerr());
		return -1;
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < cnt; i++) {
		msg[0] = MSG_EVENT;
		msg[1] = i;

		if (__mq_send(mq_req, msg, MSG_SIZE) < 0) {
			ERR("__mq_send() failed: %s.", __strerr());
			return -1;
		}

		if (__mq_recv(mq_rep, msg, MSG_SIZE) < 0) {
			ERR("__mq_recv() failed: %s.", __strerr());
			return -1;
		}
	}

	msg[0] = MSG_STOP;
	msg[1] = 0;

	if (__mq_send(mq_req, msg, MSG_SIZE) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		return -1;
	}

	if (__mq_recv(mq_rep, msg, MSG_SIZE) < 0) {
		ERR("__mq_recv() failed: %s.", __strerr());
		return -1;
	}

	gettimeofday(&end, NULL);
	print_stats(&start, &end, cnt, MSG_SIZE);

	__mq_close(mq_req);
	__mq_close(mq_rep);
	return 0;
}

int mq_server(void) 
{
	uint32_t msg[MSG_SIZE / 2 + 1];
	__mq_t mq_req;
	__mq_t mq_rep;

	printf(" - Message queue server...\n");
	fflush(stdout);

	INF("creating request message queue ...");
	__mq_unlink(mq_req_name);
	if (__mq_create(&mq_req, mq_req_name, MSG_SIZE) < 0) {
		ERR("__mq_create(\"%s\") failed: %s.", mq_req_name, __strerr());
		return -1;
	}

	INF("Waiting for start...");
	do {
		if (__mq_recv(mq_req, msg, MSG_SIZE) < 0) {
			ERR("__mq_recv() failed: %s.", __strerr());
			return -1;
		}
	} while (msg[0] != MSG_START);  

	INF("Openning reply message queue...");
	if (__mq_open(&mq_rep, mq_rep_name) < 0) {
		ERR("__mq_open(\"%s\") failed: %s.", mq_rep_name, __strerr());
		return -1;
	}

	INF("End of handshake...");
	if (__mq_send(mq_rep, msg, MSG_SIZE) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		return -1;
	}

	do {
		if (__mq_recv(mq_req, msg, MSG_SIZE) < 0) {
			ERR("__mq_recv() failed: %s.", __strerr());
			return -1;
		}

		if (__mq_send(mq_rep, msg, MSG_SIZE) < 0) {
			ERR("__mq_send() failed: %s.", __strerr());
			return -1;
		}
	} while (msg[0] != MSG_STOP);

	__mq_close(mq_req);
	__mq_close(mq_rep);

	return 0;
}

/* ---------------------------------------------------------------------------
   Pthread Mutex
   -------------------------------------------------------------------------- */

#if ENABLE_SERVER_THREAD
pthread_mutex_t  g_req_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  g_rep_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_req_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t g_rep_cond = PTHREAD_COND_INITIALIZER;
uint32_t g_req_buf[MSG_SIZE / 2 + 1];
uint32_t g_rep_buf[MSG_SIZE / 2 + 1];

int mutex_client(int cnt)
{
	uint32_t * req = g_req_buf;
	uint32_t * rep = g_rep_buf;
	struct timeval start;
	struct timeval end;
	uint32_t seq;

	printf(" - Pthread Mutex client...\n");

	gettimeofday(&start, NULL);
	for (seq = 0; seq < cnt; seq++) {
		pthread_mutex_lock(&g_req_mutex);
		req[0] = seq;
		req[1] = MSG_EVENT;
		pthread_mutex_unlock(&g_req_mutex);
		pthread_cond_signal(&g_req_cond);

	//	printf(".");
	//	fflush(stdout);

		pthread_mutex_lock(&g_rep_mutex);
		while (rep[0] != seq) {
			pthread_cond_wait(&g_rep_cond, &g_rep_mutex);
		}
		pthread_mutex_unlock(&g_rep_mutex);
	}

	/* flush */
	pthread_mutex_lock(&g_req_mutex);
	req[0] = seq;
	req[1] = MSG_SYNC;
	pthread_mutex_unlock(&g_req_mutex);
	pthread_cond_signal(&g_req_cond);

	pthread_mutex_lock(&g_rep_mutex);
	while (rep[0] != seq) {
		pthread_cond_wait(&g_rep_cond, &g_rep_mutex);
	}
	pthread_mutex_unlock(&g_rep_mutex);

	gettimeofday(&end, NULL);
	print_stats(&start, &end, cnt, MSG_SIZE);

	return 0;
}

int mutex_server(void) 
{
	volatile uint32_t * req = g_req_buf;
	volatile uint32_t * rep = g_rep_buf;
	uint32_t msg;
	int seq = 0;

	printf(" - Pthread Mutex Server...\n");
	req[0] = -1;
	rep[0] = -1;

	for (;;) {
		pthread_mutex_lock(&g_req_mutex);
		while (req[0] != seq) {
			pthread_cond_wait(&g_req_cond, &g_req_mutex);
		}
		msg = req[1];
		pthread_mutex_unlock(&g_req_mutex);
	//	printf("+");
	//	fflush(stdout);

		switch (msg) {
		case MSG_EVENT:
			pthread_mutex_lock(&g_rep_mutex);
			rep[0] = seq;
			pthread_cond_signal(&g_rep_cond);
			pthread_mutex_unlock(&g_rep_mutex);
			break;
		case MSG_SYNC:
			pthread_mutex_lock(&g_rep_mutex);
			rep[0] = seq;
			pthread_cond_signal(&g_rep_cond);
			pthread_mutex_unlock(&g_rep_mutex);
			return 0;
		}

		seq++;
	}

	return 0;
}
#else
#endif

/* ---------------------------------------------------------------------------
   Server Task
   -------------------------------------------------------------------------- */

int server_task(void)
{
	mq_server(); 
//	shm_server(); 
#if ENABLE_SERVER_THREAD
	mutex_server();
#endif

	return 0;
}

int server_start(void)
{
#if ENABLE_SERVER_THREAD
	pthread_t thread;
	int ret;

	if ((ret = pthread_create(&thread, NULL, 
							  (void * (*)(void *))server_task, 
							  (void *)NULL)) < 0) {
		fprintf(stderr, "err: pthread_create() failed: %s", strerror(ret));
		return -1;
	} 

	return 0;
#else
	int pid;

	/* fork a new process to run as server */
	if ((pid = fork()) < 0) {
		fprintf(stderr, "err: fork() fail: %s", strerror(errno));
		return -1;
	}
	
	/* parent is the client */
	if (pid == 0) {
		/* child run as server */
		server_task();
		exit(1);
	}

	return pid;
#endif
}

void server_stop(int pid)
{
#if ENABLE_SERVER_THREAD
#else
	/* stop the server */
	kill(pid, SIGTERM);
#endif
}


int main(int argc, char *argv[]) 
{
	int pid;
	int cnt = 1000000;

	printf("\n* IPC Benchmark start\n");

	pid = server_start();

	/* Wait a wile to make sure the server process initializes.
	   This is not pretty but will do for now */
	usleep(100000);
	mq_client(cnt); 
//	usleep(100000);
//	shm_client(cnt); 
#if ENABLE_SERVER_THREAD
	usleep(100000);
	mutex_client(cnt);
#endif

	/* stop the server */
	server_stop(pid);

	printf("* IPC Benchmark end.\n");
	return 0;
}


#include <errno.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>

#define __CHIME_I__
#include "chime-i.h"

#include "mempool.h"

#ifndef TRACE_RING_SIZE
#define TRACE_RING_SIZE 2048
#endif

static struct {
	__mutex_t mutex;
	struct {
		bool started;
		__thread_t thread;
	} dump;
	struct mempool * mem;
	struct {
		volatile uint32_t head;
		volatile uint32_t tail;
		struct trace_entry * buf[TRACE_RING_SIZE];
	} ring;
} __trace = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

static inline struct trace_entry * __trace_alloc(void)
{
	return memblk_alloc(__trace.mem);
}

static inline bool __trace_free(struct trace_entry * entry)
{
	return memblk_free(__trace.mem, entry);
}

int __chime_trace_init(void)
{
	__trace.mem = mempool_alloc(TRACE_RING_SIZE, sizeof(struct trace_entry));
	__mutex_init(&__trace.mutex);

	return (__trace.mem == NULL) ? -1 : 0;
}

/*
 * Insert an entry in the buffer ring
 */
void __chime_req_trace(struct chime_req_trace * req)
{
	struct trace_entry * trc;
	uint32_t head;

	if ((trc = __trace_alloc()) == NULL) {
		fprintf(stderr, "%s: __trace_alloc() failed\n", __func__);
		fflush(stderr);
		return;
	}

	trc->node_id = req->hdr.node_id;
	trc->level = req->level;
	trc->facility = req->facility;
	strncpy(trc->msg, req->msg, CHIME_TRACE_MSG_MAX - 1);
	trc->msg[CHIME_TRACE_MSG_MAX - 1] = '\0';

	__mutex_lock(__trace.mutex);

	trc->ts = __chime_clock();
	head = __trace.ring.head;
	assert((int32_t)(head - __trace.ring.tail) != TRACE_RING_SIZE);
	__trace.ring.buf[head & (TRACE_RING_SIZE - 1)] = trc;
	__trace.ring.head = head + 1;

	__mutex_unlock(__trace.mutex);
}

/*
 * Remove an entry form the buffer ring
 */
struct trace_entry * chime_trace_get(void)
{
	struct trace_entry * trc = NULL;
	uint32_t tail;

	__mutex_lock(__trace.mutex);

	tail = __trace.ring.tail;
	if (tail != __trace.ring.head) {
		trc = __trace.ring.buf[tail & (TRACE_RING_SIZE - 1)];
		__trace.ring.tail = tail + 1;
	}

	__mutex_unlock(__trace.mutex);

	return trc;
}

bool chime_trace_free(struct trace_entry * entry)
{
	return __trace_free(entry);
}

const char trace_lvl_str[][16] = {
	" ERR",
	"WARN",
	" INF",
	" MSG",
	" DBG",
	" ---"
};

void chime_trace_dump(struct trace_entry * trc)
{
	const char * lvl;
	uint64_t ts_us;
	unsigned int us;
	unsigned int ms;
	unsigned int sec;
	unsigned int min;
	unsigned int hour;

	assert(trc->level <= T_DBG);
	lvl = trace_lvl_str[trc->level];
	ts_us = trc->ts / 1000000000LL;
	ms = ts_us / 1000;
	us = ts_us - ms * 1000;
	sec = ms / 1000;
	ms -= sec * 1000;
	min = sec / 60;
	sec -= min * 60;
	hour = min / 60;
	min -= hour * 60;
	(void)us;

	printf("%4s %02d:%02d.%03d%03d %2d: %s\n", lvl, min, sec, ms, us,
		   trc->node_id, trc->msg);
	fflush(stdout);
}

static int trace_dump_task(void * arg)
{
	struct trace_entry * trc;

	__thread_init("TRACE");

	INF("trace dump thread started.");

	for (;;) {
		while ((trc = chime_trace_get()) != NULL) {
			chime_trace_dump(trc);
			chime_trace_free(trc);
		}
		__msleep(100);
	}

	return 0;
}

int chime_trace_dump_start(void)
{
	int ret = -1;

	__mutex_lock(__trace.mutex);

	if (!__trace.dump.started) {

		if ((ret = __thread_create(&__trace.dump.thread,
								   (void * (*)(void *))trace_dump_task,
								   (void *)NULL)) < 0) {
			ERR("__thread_create() failed: %s.", strerror(ret));
		} else {
			__trace.dump.started = true;
			ret = 0;
			INF("trace dump service started.");
		}
	} else {
		ERR("trace dump service alredy running.");
	}

	__mutex_unlock(__trace.mutex);

	return ret;
}

int chime_trace_dump_stop(void)
{
	int ret = 0;

	__mutex_lock(__trace.mutex);

	if (__trace.dump.started) {
		__thread_cancel(__trace.dump.thread);
		__thread_join(__trace.dump.thread, NULL);
		__trace.dump.started = false;
		INF("trace dump service finished.");
	} else {
		ret = -1;
		ERR("trace dump service not running.");
	}

	__mutex_unlock(__trace.mutex);

	return ret;
}


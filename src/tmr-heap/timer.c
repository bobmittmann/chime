/*
 * @file	net-heap.c
 * @brief	Priority queue
 * @author	Robinson Mittmann (bobmittmann@gmail.com)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "debug.h"
#include "timer.h"
#include "chime.h"

#define __PARENT(I) ((I) >> 1)
#define __LEFT(I) ((I) << 1)
#define __RIGHT(I) (((I) << 1) + 1)
#define __SIZE(HEAP) ((intptr_t)(HEAP)[0])
#define __SET_SIZE(HEAP, N) (HEAP)[0] = (struct timer *)(intptr_t)(N)

#define __KEY(HEAP, I, CLK) ((int32_t)((HEAP)[I]->clk - (CLK)))
#define __KEY_MAX() ((int32_t)INT32_MAX)

/* Insert a key/value pair into the heap, maintaining the heap property, 
   i.tmr., the minimum value is always at the top. */
bool tmr_heap_insert(struct timer * heap[], struct timer * tmr, uint32_t clk)
{	
	register unsigned int i;
	register int32_t i_key;

	/* increment the heap size by one */
	i = __SIZE(heap) + 1;
	__SET_SIZE(heap, i);

	/* insert ath the end of heap */
	heap[i] = tmr;
	tmr->pos = i;

	i_key = (int32_t)(tmr->clk - clk);
	/* sanity check */
	if (i_key > __KEY_MAX()) {
		ERR("key(%d) >  __KEY_MAX()", i_key);
	}

	/* move the entry up */
	while (i > 1) {
		register unsigned int j;
		register int32_t j_key;
		register struct timer * j_tmr;

		j = __PARENT(i);
		j_tmr = heap[j];
		j_key = (int32_t)(j_tmr->clk - clk);

		DBG("i:%d(key=%d clk=%d) j:%d(key=%d clk=%d)", i, i_key, 
			tmr->clk, j, j_key, j_tmr->clk);

		if (j_key <= i_key)
			break;

		/* exchange with parent */
		heap[i] = j_tmr;
		heap[j] = tmr;
		j_tmr->pos = i;
		tmr->pos = j;
		
		i = j;
	}

	DBG("i=%d key=%d size=%d", i, __KEY(heap, i, clk), __SIZE(heap));

	return true;
}

/* Get the minimum key from the heap */
struct timer * tmr_heap_get_min(struct timer * heap[])
{
	if (__SIZE(heap) < 1)
		return NULL;

	return heap[1];
}

void tmr_heap_heapify(struct timer * heap[], unsigned int i, uint32_t clk)
{
	register unsigned int size;
	register unsigned int l;
	register unsigned int r;

	size = __SIZE(heap);

	/* Move the i entry down the heap to preserve the heap property. */
	while ((l = __LEFT(i)) <= size) { /* end of heap ? */
		register struct timer * a;
		register struct timer * b;
		register unsigned int min;

		/* get the minimum from i, left and right. */
		min = (__KEY(heap, l, clk) < __KEY(heap, i, clk)) ? l : i;

		r = __RIGHT(i);
		if ((r <= size) && (__KEY(heap, r, clk) < __KEY(heap, min, clk)))
			min = r;

		/* if 'i' is the minimum, then we are done. */
		if (min == i)
			break;

		/* swap i with minimum. */
		a = heap[i];
		b = heap[min];
		heap[i] = b;
		heap[min] = a;
		a->pos = min;
		b->pos = i;

		/* i has moved down the heap. */
		i = min;
	}
}

/* Remove the i element from the heap. Reorder to preserve the 
   heap propriety. */
bool tmr_heap_delete(struct timer * heap[], unsigned int i, uint32_t clk)
{
	register unsigned int size;
	struct timer * tmr;

	if (i > (size = __SIZE(heap)))
		return false;

	DBG("i=%d key=%d", i, __KEY(heap, i, clk));

	tmr = heap[i];
	tmr->pos = 0;

	/* get the last item */
	tmr = heap[size];
	/* decrement the heap size by one */
	__SET_SIZE(heap, --size);

	if (size > 0) {
		/* insert the last item at 'i' */
		heap[i] = tmr;
		tmr->pos = i;
		tmr_heap_heapify(heap, i, clk);
	}

	return true;
}

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. Returns the value and the associated key */
struct timer * tmr_heap_extract_min(struct timer * heap[], uint32_t clk)
{
	struct timer * tmr;

	if (__SIZE(heap) < 1)
		return NULL;

	DBG("key=%d size=%d", __KEY(heap, 1, clk), __SIZE(heap));

	tmr = heap[1];

	if (tmr->pos != 1) {
		ERR("tmr->pos(%d) != 1", tmr->pos);
	}

	tmr_heap_delete(heap, 1, clk);

	return tmr;
}

/* Pick the key and value at position i from the heap */
struct timer * tmr_heap_pick(struct timer * heap[], unsigned int i)
{
	if (i > __SIZE(heap))
		return NULL;

	return heap[i];
}

void tmr_heap_init(struct timer * heap[])
{
	__SET_SIZE(heap, 0);
}

/****************************************************************************
  Dirty 
 ****************************************************************************/

/****************************************************************************
  Timer schedule
 ****************************************************************************/

#define TIMER_SCHED_LENGHT 64

struct tmr_sched {
	volatile bool wakeup;
	uint32_t clk;
	uint32_t min_clk;
	struct timer tmr[TIMER_SCHED_LENGHT];
	struct timer * heap[TIMER_SCHED_LENGHT + 1];
};

struct tmr_sched sched;

void timer_sched_isr(void)
{
	sched.clk++;

	if ((sched.min_clk - sched.clk) < 0) {
		DBG("wakeup...");
		/* wakeup worker thread */
		sched.wakeup = true;
	}
}

void timer_default_callback(void * param)
{
	DBG("...");
}

void timer_init(unsigned int tmr_id, void (* callback)(void *), void * param)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];

	DBG("tmr_id=%d", tmr_id);

	tmr->clk = 0;
	tmr->itval = 0;
	tmr->pos = 0;
	if (callback == NULL)
		callback = timer_default_callback;
	tmr->callback = callback;
	tmr->param = param;
}

void timer_set(unsigned int tmr_id, uint32_t timeout, uint32_t period)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];

	tmr->itval = period;
	tmr->clk = sched.clk + timeout;

	DBG("tmr_id=%d tmo=%d itv=%d clk=%d", tmr_id, timeout, period, tmr->clk);

	if ((tmr->clk - sched.min_clk) < 0)
		sched.min_clk = tmr->clk;

	if (tmr->pos == 0) {
		tmr_heap_insert(sched.heap, tmr, sched.clk);
	} else {
		if (tmr != sched.heap[tmr->pos]) {
			ERR("tmr != sched.heap[tmr->pos]");
		}
		tmr_heap_heapify(sched.heap, tmr->pos, sched.clk);
	}
}

void timer_start(unsigned int tmr_id)
{
	struct timer * tmr;

	if (tmr_id >= TIMER_SCHED_LENGHT) {
		ERR("tmr_id >= TIMER_SCHED_LENGHT");
		return;
	}

	tmr = &sched.tmr[tmr_id];

	if (tmr->pos == 0) {
		tmr_heap_insert(sched.heap, tmr, sched.clk);

		if ((tmr->clk - sched.min_clk) < 0)
			sched.min_clk = tmr->clk;
	}
}

void timer_stop(unsigned int tmr_id)
{
	struct timer * tmr;

	assert(tmr_id < TIMER_SCHED_LENGHT);

	tmr = &sched.tmr[tmr_id];

	if (tmr->pos != 0)
		tmr_heap_delete(sched.heap, tmr->pos, sched.clk);
}

void timer_sched(void)
{
	struct timer * tmr;

	if (!sched.wakeup)
		return;

	sched.wakeup = false;

	/* process timers */
	while ((tmr = tmr_heap_get_min(sched.heap)) != NULL) {
		if ((tmr->clk - sched.clk) < 0) {
			if (tmr->itval) {
				/* reschedule a periodic clock */
				tmr->clk = sched.clk + tmr->itval;
				tmr_heap_heapify(sched.heap, 1, sched.clk);
				if ((tmr->clk - sched.min_clk) < 0)
					sched.min_clk = tmr->clk;
			} else {
				/* remove a non periodic clock */
				tmr_heap_delete(sched.heap, 1, sched.clk);
			}
			tmr->callback(tmr->param);
		} else {
			/* new clock minimum */
			sched.min_clk = tmr->clk;
			break;
		}
	}
}


void timer_sched_init(void)
{
	unsigned int i;

	for (i = 0; i < TIMER_SCHED_LENGHT; ++i) {
		sched.tmr[i].pos = 0;
		sched.tmr[i].clk = 0;
		sched.tmr[i].itval = 0;
		sched.tmr[i].callback = timer_default_callback;
		sched.tmr[i].param = NULL;
	}

	tmr_heap_init(sched.heap);

	sched.wakeup = false;

	chime_tmr_init(7, timer_sched_isr, 1000, 1000);
}

/****************************************************************************
  Heap Debug
 ****************************************************************************/

void __walk(FILE* f, struct timer * heap[], int i, uint32_t clk,
			int lvl, uint32_t msk)
{
	int32_t key;
	int j;

	if (i > __SIZE(heap))
		return;

	key = __KEY(heap, i, clk);

	for (j = 0; j < lvl - 1 ; ++ j) {
		if (msk & (1 << j))
			fprintf(f, "  | ");
		else
			fprintf(f, "    ");
	}

	if (lvl > 0) {
		fprintf(f, "  +-");
	}

	fprintf(f, "[%5d]\n", key);

	__walk(f, heap, __LEFT(i), clk, lvl + 1, msk);

	msk &= ~(1 << lvl);

	__walk(f, heap, __RIGHT(i), clk, lvl + 1, msk);
}

void tmr_heap_dump(FILE* f)
{
	fprintf(f, "\n- clk: %d -------\n", sched.clk);

	__walk(f, sched.heap, 1, sched.clk, 0, 0xffffffff);

	fflush(f);
}

void tmr_heap_flush(FILE* f)
{
	struct timer ** heap = sched.heap;
	uint32_t clk = sched.clk;
	struct timer * tmr;
	int32_t key;
	int32_t prev_key;

	fprintf(f, "\n- clk: %d -------\n", clk);

	if ((tmr = tmr_heap_extract_min(heap, clk)) == NULL) {
		fprintf(f, " - heap empty!\n");
		return;
	}


	key = (int32_t)(tmr->clk - clk);
	prev_key = key;

	fprintf(f, " %d %5d\n", tmr->pos, key);

	tmr_heap_dump(f);

	while ((tmr = tmr_heap_extract_min(heap, clk)) != NULL) {
		key = (int32_t)(tmr->clk - clk);

		if (key < prev_key) {
			fprintf(f, " %d %5d <-- ERROR\n", tmr->pos, key);
		} else 
			fprintf(f, " %d %5d\n", tmr->pos, key);

		prev_key = key;
	}

	fflush(f);
}


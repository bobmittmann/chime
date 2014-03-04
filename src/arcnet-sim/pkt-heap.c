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

#include "fx-net.h"
#include "debug.h"

#define HEAP_SIZE(HEAP) (HEAP)->size
#define HEAP_LENGTH(HEAP) (HEAP)->lenght

#define HEAP_PARENT(I) ((I) >> 1)
#define HEAP_LEFT(I) ((I) << 1)
#define HEAP_RIGHT(I) (((I) << 1) + 1)

#define HEAP_KEY(HEAP, I) ((int16_t)((HEAP)->e[I].ord - (HEAP)->seq))
#define HEAP_ENTRY(HEAP, I) (HEAP)->e[I]
#define HEAP_ORD(HEAP, I) (HEAP)->e[I].ord

#define MAX_ORD(HEAP) ((uint16_t)((HEAP)->seq + INT16_MAX))
#define MAX_KEY(HEAP) ((int16_t)(MAX_ORD(HEAP) - (HEAP)->seq))

void __fx_pkt_heap_dump(FILE* f, struct pkt_heap * heap)
{
	int i;
	
	fprintf(f, "seq: %d\n", heap->seq);
	for (i = 1; i <= heap->size; ++i) {
		struct heap_entry e = HEAP_ENTRY(heap, i);
		int16_t key = e.ord - heap->seq;

		fprintf(f, "%3d: %4d %3d\n", i, e.ord, key);
	}

	fflush(f);
}

void __walk(FILE* f, struct pkt_heap * heap, int i, int lvl, uint32_t msk)
{
	int16_t key;
	int j;

	if (i > heap->size)
		return;

	key = HEAP_KEY(heap, i);

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

	__walk(f, heap, HEAP_LEFT(i), lvl + 1, msk);

	msk &= ~(1 << lvl);

	__walk(f, heap, HEAP_RIGHT(i), lvl + 1, msk);
}

void fx_pkt_heap_dump(FILE* f, struct pkt_heap * heap)
{
	fprintf(f, "\n- seq: %d -------\n", heap->seq);

	__walk(f, heap, 1, 0, 0xffffffff);

	fflush(f);
}

void fx_pkt_heap_flush(FILE* f, struct pkt_heap * heap)
{
	uint16_t seq = heap->seq;
	struct heap_entry e;
	int16_t key;
	int16_t prev_key;

	fprintf(f, "\n- seq: %d -------\n", heap->seq);

	if (!fx_pkt_heap_extract_min(heap, &e)) {
		fprintf(f, " - heap empty!\n");
		return;
	}

	key = (int16_t)(e.ord - seq);
	prev_key = key;

	fprintf(f, " %5d\n", key);

	while (fx_pkt_heap_extract_min(heap, &e)) {
		key = (int16_t)(e.ord - seq);

		if (key < prev_key) {
			fprintf(f, " %5d <-- ERROR\n", key);
		} else 
			fprintf(f, " %5d\n", key);

		prev_key = key;
	}

	fflush(f);
}

static void heap_exchange(struct pkt_heap * heap, int i, int j)
{
	struct heap_entry tmp;

	tmp = HEAP_ENTRY(heap, i);
	HEAP_ENTRY(heap, i) = HEAP_ENTRY(heap, j);
	HEAP_ENTRY(heap, j) = tmp;
}

/* recursive implementation */
void __min_heapify(struct pkt_heap * heap, int size, int i)
{
	int l;
	int r;
	int min;

	if ((l = HEAP_LEFT(i)) > size)
		return;

	min = (HEAP_KEY(heap, l) < HEAP_KEY(heap, i)) ? l : i;

	r = HEAP_RIGHT(i);
	if ((r <= size) && (HEAP_KEY(heap, r) < HEAP_KEY(heap, min)))
		min = r;

	if (min != i) {
		heap_exchange(heap, i, min);
		__min_heapify(heap, size, min);
	}
}

/* Move the i entry down the heap to preserve the heap property. */
static void min_heapify(struct pkt_heap * heap, int size, int i)
{
	int l;
	int r;
	int min;

	while ((l = HEAP_LEFT(i)) <= size) { /* end of heap ? */

		/* get the minimum from i, left and right. */
		min = (HEAP_KEY(heap, l) < HEAP_KEY(heap, i)) ? l : i;
		r = HEAP_RIGHT(i);
		if ((r <= size) && (HEAP_KEY(heap, r) < HEAP_KEY(heap, min)))
			min = r;

		/* if 'i' is the minimum, then we are done. */
		if (min == i)
			break;

		/* swap i with minimum. */
		heap_exchange(heap, i, min);

		/* i has moved down the heap. */
		i = min;
	}
}

/* Insert a key/value pair into the heap, maintaining the heap property, 
   i.e., the minimum value is always at the top. */
bool fx_pkt_heap_insert(struct pkt_heap * heap, struct heap_entry e)
{	
	uint16_t ord = e.ord;
	unsigned int i;
	int16_t key;

	if (heap->size == heap->length) {
		/* overflow */
		return false;
	}

	/* increment the heap size by one */
	i = heap->size + 1;
	/* insert ath the end of heap */
	HEAP_ENTRY(heap, i) = e;

	key = (int16_t)(ord - heap->seq);


	if (key > MAX_KEY(heap)) {
		/* new key is larger than maximum key */
		ERR("key(%d) > MAX_KEY(heap)(%d)", key, MAX_KEY(heap));
		DBG("ord=%d seq=%d .....................", ord, heap->seq);
		return false; 
	} else {
		DBG5("key(%d) <= MAX_KEY(heap)(%d)", key, MAX_KEY(heap));
	}

	/* update the heap size */
	heap->size = i;

	/* move the entry up */
	while ((i > 1) && (HEAP_KEY(heap, HEAP_PARENT(i)) > HEAP_KEY(heap, i))) {
		heap_exchange(heap, i, HEAP_PARENT(i));
		i = HEAP_PARENT(i);
	}

	return true;
}

/* Get the minimum key from the heap */
bool fx_pkt_heap_get_min(struct pkt_heap * heap, struct heap_entry * ep)
{
	if (heap->size < 1)
		return false;

	if (ep != NULL)
		*ep = HEAP_ENTRY(heap, 1);

	return true;
}

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. */
bool fx_pkt_heap_delete_min(struct pkt_heap * heap)
{
	if (heap->size < 1)
		return false;

	HEAP_ENTRY(heap, 1) = HEAP_ENTRY(heap, heap->size);

	heap->size = heap->size - 1;
	min_heapify(heap, heap->size, 1);

	return true;
}

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. Returns the value and the associated key */
bool fx_pkt_heap_extract_min(struct pkt_heap * heap, struct heap_entry * ep)
{
	if (heap->size < 1)
		return false;

	DBG5("key=%d", HEAP_KEY(heap, 1));

	if (ep != NULL)
		*ep = HEAP_ENTRY(heap, 1);

	HEAP_ENTRY(heap, 1) = HEAP_ENTRY(heap, heap->size);

	heap->size = heap->size - 1;
	min_heapify(heap, heap->size, 1);

	return true;
}

/* Remove the i element from the heap. Reorder to preserve the 
   heap propriety. */
bool fx_pkt_heap_delete(struct pkt_heap * heap, int i)
{
	if (heap->size < i)
		return false;

	HEAP_ENTRY(heap, i) = HEAP_ENTRY(heap, heap->size);

	heap->size = heap->size - 1;
	min_heapify(heap, heap->size, i);

	return true;
}

/* Pick the key and value at position i from the heap */
bool fx_pkt_heap_pick(struct pkt_heap * heap, int i, struct heap_entry * e)
{
	if (heap->size < i)
		return false;

	if (e != NULL)
		*e = HEAP_ENTRY(heap, i);

	return true;
}

int fx_pkt_heap_size(struct pkt_heap * heap)
{
	return heap->size;
}

bool fx_pkt_heap_clear(struct pkt_heap * heap)
{
	heap->size = 0;
	return true;
}

void fx_pkt_heap_init(struct pkt_heap * heap, int length)
{
	DBG("len=%d max=%d", length, HEAP_LEN_MAX);
	assert(length <= HEAP_LEN_MAX);
	heap->length = length;
	heap->size = 0;
	heap->seq = 0;
}



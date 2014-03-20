/*
 * @file	clk-heap.c
 * @brief	Priority queue
 * @author	Robinson Mittmann (bobmittmann@gmail.com)
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>

#define __CLK_HEAP__
#include "clk-heap.h"

#define HEAP_SIZE(HEAP) (HEAP)->size
#define HEAP_LENGTH(HEAP) (HEAP)->length
#define HEAP_PARENT(I) ((I) >> 1)
#define HEAP_LEFT(I) ((I) << 1)
#define HEAP_RIGHT(I) (((I) << 1) + 1)

#define HEAP_KEY(HEAP, I) (int64_t)((HEAP)->a[I].clk - (HEAP)->clk)
#define HEAP_CLK(HEAP, I) (HEAP)->a[I].clk
#define HEAP_VAL(HEAP, I) (HEAP)->a[I].val
#define HEAP_ENTRY(HEAP, I) (HEAP)->a[I]

#define MAX_CLK(HEAP) ((HEAP)->clk + (UINT64_MAX / 2))

#define MAX_KEY UINT_MAX
#define MIN_KEY 0

void heap_dump(FILE* f, struct clk_heap * heap)
{
	int i;
	
	fprintf(f, "clk: %20" PRIu64 "\n", heap->clk);
	for (i = 1; i <= HEAP_SIZE(heap); ++i) {
		int64_t key = HEAP_KEY(heap, i);
		uint64_t clk = HEAP_CLK(heap, i);
		struct chime_event ev = HEAP_VAL(heap, i);

		fprintf(f, "%3d: %20"PRIu64" %16"PRId64" %3d %s\n", i, clk, key,
				ev.node_id, __evt_opc_nm[ev.opc]);
	}

	fflush(f);
}

static inline void heap_exchange(struct clk_heap * heap, int i, int j)
{
	struct key_val tmp;

	tmp = HEAP_ENTRY(heap, i);
	HEAP_ENTRY(heap, i) = HEAP_ENTRY(heap, j);
	HEAP_ENTRY(heap, j) = tmp;
}

static inline void min_heapify(struct clk_heap * heap, int size, int i)
{
	int l;
	int r;
	int min;

	while ((l = HEAP_LEFT(i)) <= size) {

		min = (HEAP_KEY(heap, l) < HEAP_KEY(heap, i)) ? l : i;

		r = HEAP_RIGHT(i);
		if ((r <= size) && (HEAP_KEY(heap, r) < HEAP_KEY(heap, min)))
			min = r;

		if (min == i)
			break;
		heap_exchange(heap, i, min);
		i = min;
	}
}

/* Insert a key/value pair into the heap, maintaining the heap property, 
   i.e., the minimum value is always at the top. */
bool heap_insert_min(struct clk_heap * heap, uint64_t clk, 
					 struct chime_event * val)
{	
	unsigned int i;

	if (HEAP_SIZE(heap) == HEAP_LENGTH(heap)) {
		/* overflow */
		return false;
	}

	i = HEAP_SIZE(heap) + 1;
	HEAP_SIZE(heap) = i;

	HEAP_VAL(heap, i) = *val;
	HEAP_CLK(heap, i) = clk;

	while ((i > 1) && (HEAP_KEY(heap, HEAP_PARENT(i)) > HEAP_KEY(heap, i))) {
		heap_exchange(heap, i, HEAP_PARENT(i));
		i = HEAP_PARENT(i);
	}

	return true;
}

/* Get the minimum key from the heap */
bool heap_minimum(struct clk_heap * heap, uint64_t * clk, 
				  struct chime_event * val)
{
	if (HEAP_SIZE(heap) < 1)
		return false;

	if (clk != NULL)
		*clk = HEAP_CLK(heap, 1);

	if (val != NULL)
		*val = HEAP_VAL(heap, 1);

	return true;
}

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. */
bool heap_delete_min(struct clk_heap * heap)
{
	if (HEAP_SIZE(heap) < 1)
		return false;

	HEAP_ENTRY(heap, 1) = HEAP_ENTRY(heap, HEAP_SIZE(heap));

	HEAP_SIZE(heap) = HEAP_SIZE(heap) - 1;
	min_heapify(heap, HEAP_SIZE(heap), 1);

	return true;
}

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. Returns the value and the associated key */
bool heap_extract_min(struct clk_heap * heap, uint64_t * clk, 
					  struct chime_event * val)
{
	if (HEAP_SIZE(heap) < 1)
		return false;

	if (clk != NULL)
		*clk = HEAP_CLK(heap, 1);

	if (val != NULL)
		*val = HEAP_VAL(heap, 1);

	HEAP_ENTRY(heap, 1) = HEAP_ENTRY(heap, HEAP_SIZE(heap));

	HEAP_SIZE(heap) = HEAP_SIZE(heap) - 1;
	min_heapify(heap, HEAP_SIZE(heap), 1);

	return true;
}

/* Remove the i element from the heap. Reorder to preserve the 
   heap propriety. */
bool heap_delete(struct clk_heap * heap, int i)
{
	if (HEAP_SIZE(heap) < i)
		return false;

	HEAP_ENTRY(heap, i) = HEAP_ENTRY(heap, HEAP_SIZE(heap));

	HEAP_SIZE(heap) = HEAP_SIZE(heap) - 1;
	min_heapify(heap, HEAP_SIZE(heap), i);

	return true;
}

/* Pick the key and value at position i from the heap */
bool heap_pick(struct clk_heap * heap, int i, uint64_t * clk, 
			   struct chime_event * val)
{
	if (HEAP_SIZE(heap) < i)
		return false;

	if (clk != NULL)
		*clk = HEAP_CLK(heap, i);

	if (val != NULL)
		*val = HEAP_VAL(heap, i);

	return true;
}


int heap_size(struct clk_heap * heap)
{
	return HEAP_SIZE(heap);
}

/* Allocate a new heap of specified maximum length */
struct clk_heap * clk_heap_alloc(size_t length)
{
	struct clk_heap * heap;

	heap = (struct clk_heap *)malloc(sizeof(struct clk_heap) + 
								 (length + 1) * sizeof(struct key_val));
	HEAP_SIZE(heap) = 0;
	HEAP_LENGTH(heap) = length;
	heap->clk = 0LL;

	return heap;
}

bool heap_clear(struct clk_heap * heap)
{
	HEAP_SIZE(heap) = 0;
	return true;
}


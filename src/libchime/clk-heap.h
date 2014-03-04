/*****************************************************************************
 * Clock heap (private) header file
 *****************************************************************************/

#ifndef __CLK_HEAP_H__
#define __CLK_HEAP_H__

#ifndef __CLK_HEAP__
#error "Never use <clk-heap.h> directly; include <chime-i.h> instead."
#endif 

#define __CHIME_I__
#include "chime-i.h"

#include <stdint.h>

struct key_val {
	uint64_t clk;
	struct chime_event val; /* chime event */
};

struct clk_heap {
	unsigned int size;
	unsigned int length;
	uint64_t clk;
	struct key_val a[];
};


#ifdef __cplusplus
extern "C" {
#endif

/* Insert a key/value pair into the heap, maintaining the heap property, 
   i.e., the minimum value is always at the top. */
bool heap_insert_min(struct clk_heap * heap, uint64_t clk, 
					 struct chime_event * val);

/* Get the key and value at the top of the heap without removing it */
bool heap_minimum(struct clk_heap * heap, 
				  uint64_t * clk, struct chime_event * val);

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. Returns the value and the associated key */
bool heap_extract_min(struct clk_heap * heap, 
					  uint64_t * clk, struct chime_event * val);

/* Remove the i element from the heap. Reorder to preserve the 
   heap propriety. */
bool heap_delete(struct clk_heap * heap, int i);

/* Pick the key and value at position i from the heap */
bool heap_pick(struct clk_heap * heap, int i, uint64_t * clk, 
			   struct chime_event * val);

int heap_size(struct clk_heap * heap);

struct clk_heap * clk_heap_alloc(size_t length);

void heap_dump(FILE* f, struct clk_heap * heap);

bool heap_clear(struct clk_heap * heap);

bool heap_delete_min(struct clk_heap * heap);

#ifdef __cplusplus
}
#endif	

#endif /* __CLK_HEAP_H__ */


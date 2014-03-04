/*****************************************************************************
 * Flexnet Communications Network
 *****************************************************************************/

#ifndef __FX_NET_H__
#define __FX_NET_H__

#include <stdint.h>
#include <stdbool.h>

struct heap_entry {
	uint16_t ord; /* entry order */
	uint8_t dst; /* destination */
	uint8_t res;  /* reserved */
	void * pkt;   /* packet buffer */
};

#define RETRY_BITS 3 
#define RETRY_MAX ((1 << RETRY_BITS) - 1) /* must be power of 2 */
#define RETRY_MASK (RETRY_MAX)

#define PRIORITY_BITS 2 
#define PRIORITY_MAX ((1 << PRIORITY_BITS) - 1) 
#define PRIORITY_SHIFT (15 - PRIORITY_BITS)

/* The heap array is indexed from 1 to 'length'.
   To simplify the implementation the first element of
   the array is not used. */

struct pkt_heap {
	unsigned int size;
	unsigned int length;
	uint16_t seq; /* order reference (used to calculate entry keys) */
	struct heap_entry e[];
};

#define HEAP_KS (RETRY_MAX + 1)
#define HEAP_KP (1 << PRIORITY_SHIFT)
#define HEAP_LEN_MAX (HEAP_KP / HEAP_KS)

/* This function combine sequence number, priority and retry count into 
   a single order number. This is used to calculate the entry's key. */

static inline uint16_t fx_pkt_heap_mk_ord(struct pkt_heap * heap,
										  unsigned int priority, 
										  unsigned int retry) {
	uint16_t ord; 
	ord = (priority << PRIORITY_SHIFT) + heap->seq + (RETRY_MAX - retry);
	heap->seq += RETRY_MAX + 1;
	return ord;
}

/* The order number is given by:

	   ord = (KP * priority + KS * sequence + retry) MOD (1 << 16)

	Where:
		
		0 =< priority <= PRIORITY_MAX 
		0 =< retry <= RETRY_MAX
	  
		KS = (RETRY_MAX + 1) 
		KP = KS * HEAP_LEN_MAX

	To speed up things:

		KS = 8  = (1 << 3)
		KP = 8192 = (1 << 13)

	Then, the maximum heap lenght is:
		
		HEAP_LEN_MAX = KP / KS = 1024
*/


#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Packet Pool 
 *****************************************************************************/

void * fx_pkt_alloc(void);

int fx_pkt_incref(void * pkt);

int fx_pkt_decref(void * pkt);

void fx_pkt_free(void * pkt);

void fx_pkt_pool_init(void);


#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Packet Heap
 *****************************************************************************/

/* Insert a key/value pair into the heap, maintaining the heap property, 
   i.e., the minimum value is always at the top. */
bool fx_pkt_heap_insert(struct pkt_heap * heap, struct heap_entry e);

/* Get the key and value at the top of the heap without removing it */
bool fx_pkt_heap_get_min(struct pkt_heap * heap, struct heap_entry * e);

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. */
bool fx_pkt_heap_delete_min(struct pkt_heap * heap);

/* Remove the minimum key from the heap and reorder so the next 
 minimum will be at the top. Returns the value and the associated key */
bool fx_pkt_heap_extract_min(struct pkt_heap * heap, struct heap_entry * e);

/* Remove the i element from the heap. Reorder to preserve the 
   heap propriety. */
bool fx_pkt_heap_delete(struct pkt_heap * heap, int i);

/* Pick the key and value at position i from the heap */
bool fx_pkt_heap_pick(struct pkt_heap * heap, int i, struct heap_entry * e);

int fx_pkt_heap_size(struct pkt_heap * heap);

void fx_pkt_heap_dump(FILE* f, struct pkt_heap * heap);

void fx_pkt_heap_init(struct pkt_heap * heap, int length);

bool fx_pkt_heap_clear(struct pkt_heap * heap);

void fx_pkt_heap_flush(FILE* f, struct pkt_heap * heap);

#ifdef __cplusplus
}
#endif	

#endif /* __FX_NET_H__ */


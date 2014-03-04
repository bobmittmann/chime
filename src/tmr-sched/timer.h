/*****************************************************************************
 * Flexnet Communications Network
 *****************************************************************************/

#ifndef __TMR_H__
#define __TMR_H__

#include <stdint.h>
#include <stdbool.h>

struct timer {
	uint32_t clk;
	uint32_t pos;
	bool enabled;
	uint32_t itval;
	void (* callback)(void *);
	void * param;
};

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Timer API
 *****************************************************************************/

bool timer_init(unsigned int tmr_id, void (* callback)(void *), void * param);

void timer_set(unsigned int tmr_id, uint32_t timeout, uint32_t period);

void timer_start(unsigned int tmr_id);

void timer_stop(unsigned int tmr_id);

void timer_sched_init(void);

void timer_sched(void);

void tmr_heap_dump(FILE* f);

void tmr_heap_flush(FILE* f);

#ifdef __cplusplus
}
#endif	

#endif /* __TMR_H__ */


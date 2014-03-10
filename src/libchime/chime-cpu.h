/*****************************************************************************
 * CHIME internal (private) header file
 *****************************************************************************/

#ifndef __CHIME_CPU_H__
#define __CHIME_CPU_H__

#ifndef __CHIME_CPU_H__
#error "Never use <chime-i.h> directly; include <chime.h> instead."
#endif 

#include <setjmp.h>

#define __CHIME_I__
#include "chime-i.h"

struct cpu_tmr {
	void (* isr)(void);
	uint32_t timeout;
	uint32_t period;
	uint32_t seq;
	uint32_t rst_ticks;
};

struct cpu_comm {
	bool tx_busy;
	uint16_t oid;
	uint16_t rx_len;
	void * rx_buf;
	void (* eot_isr)(void);
	void (* rcv_isr)(void);
	void (* dcd_isr)(void);
};

#define CHIME_TIMER_MAX 16
#define CHIME_CPU_COMM_MAX 16

#define CHIME_CPU_FREQ_HZ 1000000

struct chime_cpu {
	struct chime_client * client;
	struct chime_node * node;
	int node_id;
	bool enabled;
	__mq_t xmt_mq;
	__mq_t rcv_mq;
	bool step_rcvd;
	jmp_buf reset_env;
	jmp_buf except_env;
	void (* rst_isr)(void);
	struct cpu_tmr tmr[CHIME_TIMER_MAX];
	struct cpu_comm comm[CHIME_CPU_COMM_MAX];
	struct srv_shared * srv_shared;
};

/* Per thread storage */
extern __thread struct chime_cpu cpu;

#ifdef __cplusplus
extern "C" {
#endif

void __cpu_except(int code);

bool __cpu_req_send(int opc, int oid);

bool __cpu_req_float_set(int opc, int oid, float val);

int __cpu_ctrl_task(struct chime_node * node);

int __cpu_sim_loop(struct chime_node * node);

#ifdef __cplusplus
}
#endif	

#endif /* __CHIME_CPU_H__ */


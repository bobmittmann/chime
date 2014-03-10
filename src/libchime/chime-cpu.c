#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdarg.h>

#define __CHIME_CPU__
#include "chime-cpu.h"

/* Per thread storage */
__thread struct chime_cpu cpu;

bool __cpu_req_send(int opc, int oid)
{
	struct chime_req_hdr req;
	int ret;

	req.node_id = cpu.node_id;
	req.opc = opc;
	req.oid = oid;

	if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_HDR_LEN)) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		return false;
	}

	return true;
}

bool __cpu_req_float_set(int opc, int oid, float val)
{
	struct chime_req_float_set req;
	int ret;

	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = opc;
	req.hdr.oid = oid;
	req.val = val;

	if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_FLOAT_SET_LEN)) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		return false;
	}

	return true;
}

void __cpu_except(int code)
{
	DBG1("code=%d...", code);
	longjmp(cpu.except_env, code);
}

void __chime_evt_reset(struct chime_event * ev)
{
	struct chime_req_init req;

	DBG4("...");

	/* reset event, request to synchronize with current session */ 
	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_INIT;
	req.hdr.oid = ev->oid;
	req.sid = ev->sid;

	if (__mq_send(cpu.xmt_mq, &req, CHIME_REQ_FLOAT_SET_LEN) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		__cpu_except(EXCEPT_MQ_SEND);
	}

	longjmp(cpu.reset_env, 1);
}

void __chime_evt_step(struct chime_event * ev)
{
	DBG1("cycles=%u", (uint32_t)cpu.node->ticks);
	cpu.step_rcvd = true;
}

static void __timer_reload(struct cpu_tmr  * tmr, int tmr_id)
{
	struct chime_req_timer req;
	int ret;

	if (tmr->timeout > 0) {
		/* reschedule the timer */
		req.hdr.node_id = cpu.node_id;
		req.hdr.opc = CHIME_REQ_TMR0 + tmr_id;
		req.hdr.oid = 0;
		req.ticks = tmr->timeout;
		req.seq = ++tmr->seq;
		DBG2("tmr=%d ticks=%d.", tmr_id, req.ticks);
		if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_TIMER_LEN)) < 0) {
			ERR("__mq_send() failed: %s.", __strerr());
			__cpu_except(EXCEPT_MQ_SEND);
		} 
	}

	tmr->rst_ticks = cpu.node->ticks;
	tmr->timeout = tmr->period;
}

void __chime_evt_timer(struct chime_event * ev)
{
	uint32_t seq = ev->seq;
	struct cpu_tmr  * tmr;
	int tmr_id;

	DBG2("%d CPU cycles.", (uint32_t)cpu.node->ticks);

	tmr_id = ev->opc - CHIME_EVT_TMR0;

	assert(tmr_id >= 0);
	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];

	if (tmr->seq != seq) {
		/* Sequences don't mach. This mean this is an old event.
		   There must be a new event on the queue or the timer was
		   stopped. */
		return;
	}

	__timer_reload(tmr, tmr_id);

	if (tmr->isr != NULL)
		tmr->isr();
}

void __chime_evt_comm_eot(struct chime_event * ev)
{
	int comm_oid = ev->oid;
	struct cpu_comm * comm;
	int chan;

	chan = ev->opc - CHIME_EVT_EOT0;
	comm = &cpu.comm[chan];

	assert(comm != NULL);
	(void)comm_oid;
	assert(comm_oid == comm->oid);


	DBG1("<%d> COMM{chan=%d oid=%d}.", ev->node_id, chan, comm_oid);

	assert(comm->tx_busy == true);

	comm->tx_busy = false;


	if (comm->eot_isr != NULL)
		comm->eot_isr();
}

void __chime_evt_comm_rcv(struct chime_event * ev)
{
	int comm_oid = ev->oid;
	int i;

	DBG1("<%d> COMM{oid=%d} buf{oid=%d len=%d}.", 
		 ev->node_id, comm_oid, ev->buf.oid, ev->buf.len);

	/* FIXME: speed this up */
	for (i = 0; i < CHIME_CPU_COMM_MAX; ++i) {
		struct cpu_comm * comm = &cpu.comm[i];

		if (comm_oid == comm->oid) {
			comm->rx_buf = obj_getinstance(ev->buf.oid);
			comm->rx_len = ev->buf.len;
			if (comm->rcv_isr != NULL)
				comm->rcv_isr();
			else
				obj_decref(comm->rx_buf); /* release the buffer */
			return;
		}
	}

	__cpu_except(EXCEPT_INVALID_COMM_RCV);
}

void __chime_evt_comm_dcd(struct chime_event * ev)
{
	int comm_oid = ev->oid;
	int i;

	DBG1("<%d> COMM{oid=%d}.", 
		 ev->node_id, comm_oid);

	/* FIXME: speed this up */
	for (i = 0; i < CHIME_CPU_COMM_MAX; ++i) {
		struct cpu_comm * comm = &cpu.comm[i];

		if (comm_oid == comm->oid) {
			if (comm->dcd_isr != NULL)
				comm->dcd_isr();
			return;
		}
	}

	__cpu_except(EXCEPT_INVALID_COMM_DCD);
}


void __chime_evt_join(struct chime_event * ev)
{
	assert(ev->node_id == cpu.node->id);
	cpu.node_id = ev->node_id;
//	cpu.node->sid = ev->sid;

	DBG1("node_id=%d sid=%d.", ev->node_id, ev->sid);
}

void __chime_evt_probe(struct chime_event * ev)
{
	assert(ev->node_id == cpu.node->id);

	cpu.node->probe_seq = ev->seq;

	DBG1("<%d> seq=%d.", ev->node_id, ev->seq);
}

void __chime_evt_kick_out(struct chime_event * ev)
{
	__cpu_except(EXCEPT_KICK_OUT);
	INF("...");
}

const char * chime_cpu_name(void)
{
	return cpu.node->name;
}

int chime_cpu_id(void)
{
	return cpu.node_id;
}

uint32_t chime_cpu_cycles(void)
{
	return cpu.node->ticks;
}

void chime_tmr_init(int tmr_id, void (* isr)(void), 
					uint32_t timeout, uint32_t period)
{
	struct cpu_tmr  * tmr;

	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];

	tmr->isr = isr;
	tmr->seq = 0;
	tmr->timeout = timeout;
	tmr->period = period;

	__timer_reload(tmr, tmr_id);
}

void chime_tmr_stop(int tmr_id)
{
	struct cpu_tmr  * tmr;

	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];
	tmr->seq++;
	tmr->timeout = 0;
	tmr->period = 0;
}

void chime_tmr_reset(int tmr_id, uint32_t timeout, uint32_t period)
{
	struct cpu_tmr  * tmr;

	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];

	tmr->timeout = timeout;
	tmr->period = period;

	__timer_reload(tmr, tmr_id);
}

void chime_tmr_start(int tmr_id)
{
	struct cpu_tmr  * tmr;

	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];
	(void)tmr;

	assert(tmr->timeout > 0);

	__timer_reload(tmr, tmr_id);
}

uint32_t chime_tmr_count(int tmr_id)
{
	struct cpu_tmr  * tmr;

	assert(tmr_id < CHIME_TIMER_MAX);

	tmr = &cpu.tmr[tmr_id];
	(void)tmr;

	return cpu.node->ticks - tmr->rst_ticks;
}

static void __cpu_event_wait(void)
{       
	struct chime_event buf;
	struct chime_event * evt = (struct chime_event *)&buf;
	int len;

again:

	if ((len = __mq_recv(cpu.rcv_mq, evt, CHIME_EVENT_LEN)) < 0) {
		ERR("__mq_recv() failed: %s!", __strerr());
		__cpu_except(EXCEPT_MQ_RECV);
	}

	DBG5("rcvd %d bytes.", (int)len);
	DBG1("<%d> [%s]", evt->node_id, __evt_opc_nm[evt->opc]);

	switch (evt->opc) {
	case CHIME_EVT_TMR0:
	case CHIME_EVT_TMR1:
	case CHIME_EVT_TMR2:
	case CHIME_EVT_TMR3:
	case CHIME_EVT_TMR4:
	case CHIME_EVT_TMR5:
	case CHIME_EVT_TMR6:
	case CHIME_EVT_TMR7:
		__chime_evt_timer(evt);
		break;
	case CHIME_EVT_EOT0:
	case CHIME_EVT_EOT1:
	case CHIME_EVT_EOT2:
	case CHIME_EVT_EOT3:
	case CHIME_EVT_EOT4:
	case CHIME_EVT_EOT5:
	case CHIME_EVT_EOT6:
	case CHIME_EVT_EOT7:
		__chime_evt_comm_eot(evt);
		break;
	case CHIME_EVT_RCV:
		__chime_evt_comm_rcv(evt);
		break;
	case CHIME_EVT_DCD:
		__chime_evt_comm_dcd(evt);
		break;
	case CHIME_EVT_JOIN:
		__chime_evt_join(evt);
		break;
	case CHIME_EVT_KICK_OUT:
		__chime_evt_kick_out(evt);
		break;
	case CHIME_EVT_RESET:
		__chime_evt_reset(evt);
		break;
	case CHIME_EVT_STEP:
		__chime_evt_step(evt);
		break;
	case CHIME_EVT_PROBE:
		__chime_evt_probe(evt);
		goto again;
	default:
		__cpu_except(EXCEPT_INVALID_EVENT);
	}
}

void chime_cpu_wait(void)
{
	struct chime_req_bkpt req;
	int ret;

	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_BKPT;
	req.hdr.oid = 0;

	DBG2("break...");
	if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_BKPT_LEN)) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		__cpu_except(EXCEPT_MQ_SEND);
	} 
	
	__cpu_event_wait();
	DBG2("run...");
}

int __cpu_sim_loop(struct chime_node * node)
{       
	struct chime_req_abort req;
	int code;

	/* initialize local thread storage variables */
	cpu.client = node->c.client;
	cpu.srv_shared = node->c.srv_shared;
	cpu.rcv_mq = node->c.rcv_mq;
	cpu.xmt_mq = node->c.xmt_mq;
	cpu.rst_isr = node->c.on_reset;
	cpu.enabled = true;
	cpu.node_id = -1;
	cpu.node = node;

	DBG1("CPU:%s control init.", cpu.node->name);
	
	if ((code = setjmp(cpu.except_env)) > 0) {
		ERR("exception: %d.", code);
	} else {
		if (setjmp(cpu.reset_env)) {
			cpu.rst_isr();
		}

		DBG1("CPU:%s control loop...", cpu.node->name);

		for (;;) {
			__cpu_event_wait();
		}
	}

	DBG("RIP. <%d> died with code %d.", cpu.node_id, code);

	/* notify server */
	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_ABORT;
	req.hdr.oid = obj_oid(cpu.node);
	req.code = code;
	if (__mq_send(cpu.xmt_mq, &req, CHIME_REQ_ABORT_LEN) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
	} 

	/* notify client */
	cpu.enabled = false;
	node->c.except = code;

	__sem_post(node->c.except_sem);

	DBG2("CPU:%s control end.", cpu.node->name);

	return 0;
}

int __cpu_ctrl_task(struct chime_node * node)
{       
	/* initialize thread */
	__thread_init("CPU");

	return __cpu_sim_loop(node);
}

void chime_cpu_step(uint32_t cycles)
{
	struct chime_req_step req;

	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_STEP;
	req.hdr.oid = 0;
	req.cycles = cycles;

	DBG1("cycles=%d.", cycles);

	if (__mq_send(cpu.xmt_mq, &req, CHIME_REQ_STEP_LEN) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		__cpu_except(EXCEPT_MQ_SEND);
	} 

	cpu.step_rcvd = false;
	__cpu_event_wait();

	while (!cpu.step_rcvd) {
		chime_cpu_wait();
	}
}

void chime_cpu_self_destroy(void) 
{
	__cpu_except(EXCEPT_SELF_DESTROYED);
}

bool tracef(int lvl, const char * __fmt, ...)
{
	struct chime_req_trace req;
	va_list ap;
	int ret;
	int n;

	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_TRACE;
	req.level = lvl;
	req.facility = 0;

	va_start(ap, __fmt);
	n = vsnprintf(req.msg, CHIME_TRACE_MSG_MAX - 1, __fmt, ap);
	req.msg[n++] = '\0';
	va_end(ap);

	if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_TRACE_LEN(n))) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
	}

	return (ret < 0) ? false : true;
}

static int __var_open(const char * name)
{
	struct srv_shared * shared = cpu.srv_shared;
	struct chime_var * var;
	int oid;

	DBG1("name=%s", name);

	objpool_lock();
	oid = __dir_lst_lookup(&shared->dir, name);
	objpool_unlock();

	if (oid != OID_NULL) {
		return oid;
	} 

	if ((var = obj_alloc()) == NULL) {
		ERR("object allocation failed!");
		return -1;
	}		

	oid = obj_oid(var);
	DBG1("oid=%d", oid);
	
	/* initialize object */
	objpool_lock();
	strncpy(var->name, name, ENTRY_NAME_MAX);
	var->clk = 0;
	var->cnt = 0;
	var->len = 0;
	var->rec = NULL;
	objpool_unlock();

	if (__cpu_req_send(CHIME_REQ_VAR_CREATE, oid)) {
		/* insert into directory */
		objpool_lock();
		__dir_lst_insert(&shared->dir, name, oid);
		objpool_unlock();
	} else {
		/* release the object */
		free(var->rec);
		obj_free(var);
		return -1;
	}

	return oid;
}

int chime_var_open(const char * name)
{
	assert(name != NULL);
	assert(strlen(name) < ENTRY_NAME_MAX);

	return __var_open(name);
}

int chime_cpu_var_open(const char * name)
{
	char xname[ENTRY_NAME_MAX + 1];

	DBG("<%d> name='%s'", cpu.node_id, name);

	assert(name != NULL);
	assert(strlen(name) < (ENTRY_NAME_MAX - 2));

	sprintf(xname, "%s%02x", name, cpu.node_id);

	return __var_open(xname);
}

int chime_var_close(int oid)
{
	assert(oid > OID_NULL);

	ERR("not implemented!");
	return 0;
}

bool chime_var_rec(int oid, double value)
{
	struct chime_req_var_rec req;
	int ret;

	DBG5("<%d> val=%f.", cpu.node_id, value);

	assert(oid > OID_NULL);

	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_VAR_REC;
	req.hdr.oid = oid;
	req.val = value;

	if ((ret = __mq_send(cpu.xmt_mq, &req, CHIME_REQ_VAR_REC_LEN)) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
	}

	return (ret < 0) ? false : true;
}

double chime_cpu_time(void)
{
	DBG5("time=%.9f", cpu.node->time);

	return cpu.node->time;
}


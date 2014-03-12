/*****************************************************************************
 * CHIME internal (private) header file
 *****************************************************************************/

#ifndef __CHIME_I_H__
#define __CHIME_I_H__

#ifndef __CHIME_I__
#error "Never use <chime-i.h> directly; include <chime.h> instead."
#endif 

#define _GNU_SOURCE
#include <stdlib.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#define _WIN32_WINNT 0x0600
#include <windows.h>
#endif

#ifdef _WIN32
#define    MIN(a,b)    (((a)<(b))?(a):(b))
#define    MAX(a,b)    (((a)>(b))?(a):(b))
#else
#include <unistd.h>
#include <mqueue.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <debug.h>
#include <chime.h>
#include "objpool.h"

#ifdef _WIN32
typedef HANDLE __mq_t;
typedef HANDLE __shm_t;
typedef HANDLE __mutex_t;
typedef HANDLE __sem_t;
typedef HANDLE __thread_t;
typedef HANDLE __fd_t;
#else
typedef mqd_t __mq_t;
typedef int __shm_t;
typedef sem_t * __mutex_t;
typedef sem_t * __sem_t;
typedef pthread_t * __thread_t;
typedef int __fd_t;
#endif

#define OBJPOOL_OBJ_SIZE_MAX (1024 - 4)

#define USEC (1000000000LL)
#define MSEC (1000000000000LL)
#define SEC  (1000000000000000LL)

#define TS2MSEC(X) (uint64_t)((uint64_t)(X)/MSEC)
#define TS2USEC(X) (uint64_t)((uint64_t)(X)/USEC)
#define TS2F(X) ((double)(X)/1e15) 

/*****************************************************************************
 * Chime RPC requests
 *****************************************************************************/

/* Request codes */
enum {
	CHIME_REQ_TMR0 = 0,
	CHIME_REQ_TMR1,
	CHIME_REQ_TMR2,
	CHIME_REQ_TMR3,

	CHIME_REQ_TMR4,
	CHIME_REQ_TMR5,
	CHIME_REQ_TMR6,
	CHIME_REQ_TMR7,

	CHIME_REQ_XMT0,
	CHIME_REQ_XMT1,
	CHIME_REQ_XMT2,
	CHIME_REQ_XMT3,

	CHIME_REQ_XMT4,
	CHIME_REQ_XMT5,
	CHIME_REQ_XMT6,
	CHIME_REQ_XMT7,

	CHIME_REQ_INIT,
	CHIME_REQ_BKPT,
	CHIME_REQ_STEP,
	CHIME_REQ_COMM_CREATE,

	CHIME_REQ_JOIN,
	CHIME_REQ_BYE,
	CHIME_REQ_ABORT,
	CHIME_REQ_TRACE,

	CHIME_REQ_RESET_ALL,
	CHIME_REQ_SIM_SPEED_SET,
	CHIME_REQ_SIM_TEMP_SET,
	CHIME_SIG_SIM_TICK,

	CHIME_SIG_SIM_PAUSE,
	CHIME_SIG_SIM_RESUME,
	CHIME_SIG_SIM_STEP,
	CHIME_REQ_COMM_STAT,

	CHIME_REQ_VAR_CREATE,
	CHIME_REQ_VAR_REC,
	CHIME_REQ_VAR_DUMP,
	CHIME_REQ_CPU_RESET
};

static const char __req_opc_nm[][16] = {
	"TMR0",
	"TMR1",
	"TMR2",
	"TMR3",

	"TMR4",
	"TMR5",
	"TMR6",
	"TMR7",

	"XMT0",
	"XMT1",
	"XMT2",
	"XMT3",

	"XMT4",
	"XMT5",
	"XMT6",
	"XMT7",

	"INIT",
	"BKPT",
	"STEP",
	"COMM CREATE",

	"JOIN",
	"BYE",
	"ABORT",
	"TRACE",

	"RESET ALL",
	"SIM SPEED",
	"SIM TEMP",
	"SIM TICK",

	"SIM PAUSE",
	"SIM RESUME",
	"SIM STEP",
	"COMM STAT",

	"VAR CREATE",
	"REC",
	"DUMP",
	"CPU RESET"
};

/* Request header */
struct chime_req_hdr {
	uint8_t node_id;
	int8_t opc;
	uint16_t oid;
} __attribute__((aligned(4)));

#define CHIME_REQ_HDR_LEN sizeof(struct chime_req_hdr)
#define CHIME_REQ_LEN(X) (CHIME_REQ_HDR_LEN + sizeof(struct X))

/* Join request operation */
struct chime_req_join {
	struct chime_req_hdr hdr;
} __attribute__((aligned(4)));

#define CHIME_REQ_JOIN_LEN CHIME_REQ_LEN(chime_req_join)

/* Init request operation. 
   This is a response from a reset event. */
struct chime_req_init {
	struct chime_req_hdr hdr;
	uint32_t sid;
} __attribute__((aligned(4)));

#define CHIME_REQ_INIT_LEN CHIME_REQ_LEN(chime_req_init)

/* Step request operation */
struct chime_req_step {
	struct chime_req_hdr hdr;
	uint32_t cycles;
} __attribute__((aligned(4)));

#define CHIME_REQ_STEP_LEN CHIME_REQ_LEN(chime_req_step)

/* Timer register request */
struct chime_req_timer {
	struct chime_req_hdr hdr;
	uint32_t ticks;
	uint32_t seq;
} __attribute__((aligned(4)));

#define CHIME_REQ_TIMER_LEN CHIME_REQ_LEN(chime_req_timer)

/* Breakpoint register request */
struct chime_req_bkpt {
	struct chime_req_hdr hdr;
} __attribute__((aligned(4)));

#define CHIME_REQ_BKPT_LEN CHIME_REQ_LEN(chime_req_bkpt)

/* Communication request operation */
struct chime_req_comm {
	struct chime_req_hdr hdr;
	uint16_t buf_oid;
	uint16_t buf_len;
} __attribute__((aligned(4)));

#define CHIME_REQ_COMM_LEN CHIME_REQ_LEN(chime_req_comm)

/* Temperature set request operation */
/* Simulation Speed set request operation */
struct chime_req_float_set {
	struct chime_req_hdr hdr;
	float val;
} __attribute__((aligned(4)));

#define CHIME_REQ_FLOAT_SET_LEN CHIME_REQ_LEN(chime_req_float_set)


/* Variable record */
struct chime_req_var_rec {
	struct chime_req_hdr hdr;
	double val;
} __attribute__((aligned(4)));

#define CHIME_REQ_VAR_REC_LEN CHIME_REQ_LEN(chime_req_var_rec)

/* TRace request operation */
struct chime_req_trace {
	struct chime_req_hdr hdr;
	uint8_t level;
	uint8_t facility;
	char msg[CHIME_TRACE_MSG_MAX];
} __attribute__((aligned(4)));

#define CHIME_REQ_TRACE_LEN(N) (CHIME_REQ_HDR_LEN + 2 + (N))

/* Abort request operation */
struct chime_req_abort {
	struct chime_req_hdr hdr;
	uint32_t code;
} __attribute__((aligned(4)));

#define CHIME_REQ_ABORT_LEN CHIME_REQ_LEN(chime_req_abort)

/* Agregated request helper */
struct chime_request {
	union {
		struct {
			uint8_t node_id;
			int8_t opc;
			uint16_t oid;
		};
		struct chime_req_join join;
		struct chime_req_step step;
		struct chime_req_trace trace;
		struct chime_req_comm comm;
		struct chime_req_timer timer;
		struct chime_req_float_set temp;
		struct chime_req_float_set speed;
		struct chime_req_abort abort;
		struct chime_req_init init;
		struct chime_req_var_rec rec;
	};
} __attribute__((aligned(4)));

#define CHIME_REQUEST_LEN sizeof(struct chime_request)

/*****************************************************************************
 * Chime RPC responses
 *****************************************************************************/

enum {
	CHIME_EVT_TMR0 = 0,
	CHIME_EVT_TMR1,
	CHIME_EVT_TMR2,
	CHIME_EVT_TMR3,
	CHIME_EVT_TMR4,
	CHIME_EVT_TMR5,
	CHIME_EVT_TMR6,
	CHIME_EVT_TMR7,
	CHIME_EVT_EOT0,
	CHIME_EVT_EOT1,
	CHIME_EVT_EOT2,
	CHIME_EVT_EOT3,
	CHIME_EVT_EOT4,
	CHIME_EVT_EOT5,
	CHIME_EVT_EOT6,
	CHIME_EVT_EOT7,
	CHIME_EVT_RCV,
	CHIME_EVT_DCD,
	CHIME_EVT_JOIN,
	CHIME_EVT_KICK_OUT,
	CHIME_EVT_RESET,
	CHIME_EVT_STEP,
	CHIME_EVT_PROBE
};

static const char __evt_opc_nm[][8] = {
	"TMR0",
	"TMR1",
	"TMR2",
	"TMR3",
	"TMR4",
	"TMR5",
	"TMR6",
	"TMR7",
	"EOT0",
	"EOT1",
	"EOT2",
	"EOT3",
	"EOT4",
	"EOT5",
	"EOT6",
	"EOT7",
	"RCV",
	"DCD",
	"JOIN",
	"KICK",
	"RESET",
	"STEP",
	"PROB"
};

struct chime_event {
	uint8_t node_id;
	int8_t opc;
	uint16_t oid;
	union {
		uint16_t u16[2];
		uint32_t u32;
		uint32_t ticks;
		uint32_t seq;
		uint32_t sid; /* session id */
		struct {
			uint16_t oid;
			uint16_t len;
		} buf;
	};
} __attribute__((aligned(4)));

#define CHIME_EVENT_LEN sizeof(struct chime_event)

/*****************************************************************************
 * Chime Client CPU  
 *****************************************************************************/

struct cpu_info {
};


/*****************************************************************************
 * Chime directory list
 *****************************************************************************/

#define ENTRY_NAME_MAX 12
#define SRV_SHARED_OID 1

struct dir_lst {
	uint8_t cnt;
	struct { 
		uint16_t oid;
		char name[ENTRY_NAME_MAX];
	} entry[0];
};

#define SRV_SHARED_MAGIC 0xbeadc0de

struct srv_shared {
	uint32_t magic;
	double time; /* simulation time */
	struct dir_lst dir;
};

/*****************************************************************************
 * Node
 *****************************************************************************/

struct chime_node {
	uint8_t id;
	char name[63];
	float offs_ppm;
	float tc_ppm; /* temperature coeficient */
	bool bkpt; /* waiting for an event */
	double temperature;
	double tc; /* temperature constant */
	double dres; /* clock resolution in femptoseconds/microssecond */
	double period; /* temperature corrected resolution */
	uint64_t dt; /* temperature corrected clock resolution */
	uint64_t clk;
	uint64_t ticks;
	double time;
	uint32_t sid; /* session id */
	volatile uint32_t probe_seq; /* probe sequence number */
	struct {
		struct chime_client * client;
		struct srv_shared * srv_shared;
		__mq_t rcv_mq;
		__mq_t xmt_mq;
		__thread_t thread;
		int except;
		__sem_t except_sem;
		void (* on_reset)(void);
	} c; /* client side only */

	struct {
		__mq_t evt_mq;
	} s; /* server side only */
};

#define CHIME_NODE_MAX 255

/*****************************************************************************
 * Random number generators state
 *****************************************************************************/

struct exp_rand_state {
	uint64_t seed;
	double x0;
	double x1;
	double scale;
};

/*****************************************************************************
 * Comm 
 *****************************************************************************/

#define CHIME_COMM_MAX CHIME_NODE_MAX 
#define COMM_STAT_BINS 256

struct chime_comm {
	struct comm_attr attr;
	char name[ENTRY_NAME_MAX];
	uint8_t node_lst[CHIME_NODE_MAX + 1];
	uint32_t cnt;
	uint64_t randseed0;
	uint64_t randseed1;
	struct exp_rand_state exprnd;
	uint64_t bin_delay;
	uint64_t fix_delay;
	double bit_time;
	uint32_t * stat;
};

/*****************************************************************************
 * Variable 
 *****************************************************************************/

struct var_rec {
	double t;
	double y;
};

#define CHIME_VAR_MAX CHIME_NODE_MAX 

struct chime_var {
	char name[ENTRY_NAME_MAX];
	double time;
	uint64_t clk;
	uint32_t pos;
	uint32_t cnt;
	uint32_t len;
	FILE * f_dat;
	struct var_rec * rec;
};

/*****************************************************************************
 * Timer
 *****************************************************************************/

struct chime_timer {
	int timer_id;
	int node_id;
	bool enabled;
	uint32_t compare;
	uint32_t period;
};

/*****************************************************************************
 * Chime Message Queue OS wrappers
 *****************************************************************************/

static inline int __mq_send(__mq_t mq, const void * msg, size_t len)
{
	int ret;
#ifdef _WIN32
	DWORD dwWritten;
	if (WriteFile(mq, (LPTSTR)msg, len, &dwWritten, NULL))
		ret = dwWritten;
	else
		ret = -1;
#else
	ret = mq_send(mq, (char *)msg, len, 0);
#endif
	return ret;

}

static inline int __mq_recv(__mq_t mq, void * msg, size_t len)
{
	int ret;
#ifdef _WIN32
	DWORD dwRead;
	if (ReadFile(mq, (LPTSTR)msg, len, &dwRead, NULL))
		ret = dwRead;
	else
		ret = -1;
#else
	ret = mq_receive(mq, (char *)msg, len, NULL);
#endif
	return ret;
}

struct ratio {
	int32_t p;
	uint32_t q;
};

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Message queues
 *****************************************************************************/

int __mq_create(__mq_t * qp, const char * name, unsigned int maxmsg);

int __mq_open(__mq_t * qp, const char * name);

void __mq_close(__mq_t mq);

void __mq_unlink(const char * name);

/*****************************************************************************
 * Shared memory 
 *****************************************************************************/

int __shm_create(__shm_t * pshm, const char * name, size_t size);

int __shm_open(__shm_t * pshm, const char * name);

void * __shm_mmap(__shm_t shm);

int __shm_munmap(__shm_t shm, void * ptr);

void __shm_close(__shm_t shm);

void __shm_unlink(const char * name);

/*****************************************************************************
 * Mutex
 *****************************************************************************/

int __mutex_create(__mutex_t * pmtx, const char * name);

int __mutex_open(__mutex_t * pmtx, const char * name);

int __mutex_init(__mutex_t * pmtx);

int __mutex_lock(__mutex_t mtx);

int __mutex_unlock(__mutex_t mtx);

void __mutex_close(__mutex_t mtx);

void __mutex_unlink(const char * name);

/*****************************************************************************
 * Sempahore
 *****************************************************************************/

int __sem_create(__sem_t * psem, const char * name, unsigned int value);

void __sem_unlink(const char * name);

int __sem_open(__sem_t * psem, const char * name);

int __sem_init(__sem_t * psem, int pshared, unsigned int value);

void __sem_close(__sem_t sem);

int __sem_wait(__sem_t sem);

int __sem_post(__sem_t sem);

/*****************************************************************************
 * Threads
 *****************************************************************************/

int __thread_create(__thread_t * pthread, void *(* task)(void*), void * arg);

void __thread_init(const char * name);

__thread_t __thread_self(void);

int __thread_cancel(__thread_t thread);

int __thread_join(__thread_t thread, void ** value_ptr);

/*****************************************************************************
 * Error
 *****************************************************************************/

const char * __strerr(void);

/*****************************************************************************
 * Interval timer
 *****************************************************************************/

int __itmr_init(uint32_t period, void (* isr)(void));

int __itmr_stop(void);

/*****************************************************************************
 * Trace
 *****************************************************************************/

void __chime_req_trace(struct chime_req_trace * req);

int __chime_trace_init(void);

/*****************************************************************************
 * Other...
 *****************************************************************************/

void __msleep(unsigned int ms);

uint64_t __chime_clock(void);

void __term_sig_handler(void (* handler)(void));

void __dir_lst_clear(struct dir_lst * lst);

uint16_t __dir_lst_lookup(struct dir_lst * lst, const char * name); 

bool __dir_lst_insert(struct dir_lst * lst, const char * name, int oid); 

/*****************************************************************************
 * Random number generators
 *****************************************************************************/

double unif_rand(uint64_t * seedp);

double norm_rand(uint64_t * seedp);

void exp_rand_init(struct exp_rand_state * erp, double z, uint64_t seed);

double exp_rand(struct exp_rand_state * erp);

/*****************************************************************************
 * Bitmap allocator 
 *****************************************************************************/

void __bmp_alloc_init(uint64_t bmp[], int len);

int __bmp_bit_alloc(uint64_t bmp[], int len);

int __bmp_bit_free(uint64_t bmp[], int len, int bit);

/*****************************************************************************
 * Math...
 *****************************************************************************/

bool float_to_ratio(struct ratio * r, double x, uint32_t maxden);

#ifdef __cplusplus
}
#endif	

#endif /* __CHIME_I_H__ */


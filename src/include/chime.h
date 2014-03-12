/* libchime public header file */

#ifndef __CHIME_H__
#define __CHIME_H__

#include <stdint.h>
#include <stdbool.h>

/*****************************************************************************
 * Chime trace 
 *****************************************************************************/

#define CHIME_TRACE_MSG_MAX 116

enum {
	T_ERR = 0, 
	T_WARN ,
	T_INF,
	T_MSG,
	T_DBG
};

struct trace_entry {
	uint64_t ts;
	uint8_t node_id;
	uint8_t level;
	uint8_t facility;
	uint8_t res;
	char msg[CHIME_TRACE_MSG_MAX];
};

/*****************************************************************************
 * Chime communication channels 
 *****************************************************************************/

struct comm_attr {
	uint32_t wr_cyc_per_byte;
	uint32_t wr_cyc_overhead;
	uint32_t rd_cyc_per_byte;
	uint32_t rd_cyc_overhead;
	uint16_t bits_overhead;
	uint8_t bits_per_byte;
	uint8_t nodes_max;
	uint16_t bytes_max;
	float speed_bps; /* bits per second */
	float max_jitter; /* seconds */
	float min_delay;  /* minimum delay in seconds */
	float nod_delay;  /* per node delay in seconds */
	bool hist_en; /* enable histogram */
	bool txbuf_en; /* enable buffering for transmission */
	bool dcd_en; /* enable data carrier detect */
	bool exp_en; /* enable exponential distribution */
};

/*****************************************************************************
 * Chime CPU exception
 *****************************************************************************/

enum {
	EXCEPT_COMM_LOOKUP_FAIL = 1,
	EXCEPT_SELF_DESTROYED,
	EXCEPT_MQ_SEND,
	EXCEPT_MQ_RECV,
	EXCEPT_INVALID_TIMER,
	EXCEPT_INVALID_COMM_RCV,
	EXCEPT_INVALID_COMM_DCD,
	EXCEPT_KICK_OUT,
	EXCEPT_INVALID_EVENT,
	EXCEPT_OBJ_ALLOC_FAIL,
	EXCEPT_COMM_TX_BUSY
};

struct chime_except {
	uint8_t node_id;
	uint8_t code;
	uint16_t oid;
	uint64_t cycles;
};

#ifdef __cplusplus
extern "C" {
#endif


/*****************************************************************************
 * Chime Client
 *****************************************************************************/
int chime_client_start(const char * name);

bool chime_except_catch(struct chime_except * e);

int chime_client_stop(void);

/*****************************************************************************
 * Chime remote control 
 *****************************************************************************/

bool chime_reset_all(void);
bool chime_sim_speed_set(float val);
bool chime_sim_resume(void);
bool chime_sim_pause(void);
bool chime_sim_vars_dump(void);

/*****************************************************************************
 * Chime utilities
 *****************************************************************************/

void chime_sleep(unsigned int sec);

void chime_msleep(unsigned int ms);

void chime_app_init(void (* on_cleanup)(void));

/*****************************************************************************
 * Chime CPU
 *****************************************************************************/

/* Run a CPU in the main thread */
int chime_cpu_run(float offs_ppm, float tc_ppm, void (* on_reset)(void));

/* Create a new CPU on a new thread */
int chime_cpu_create(float offs_ppm, 
					 float tc_ppm, void (* reset)(void));

bool chime_cpu_reset(int cpu_oid);

void chime_cpu_step(uint32_t cycles);

void chime_cpu_wait(void);

void chime_cpu_self_destroy(void);

const char * chime_cpu_name(void);

int chime_cpu_id(void);

uint32_t chime_cpu_cycles(void);

double chime_cpu_time(void);

bool chime_cpu_temp_set(float temp);

float chime_cpu_temp_get(void);

int chime_cpu_var_open(const char * name);

float chime_cpu_freq_get(void);

float chime_cpu_ppm_get(void);

bool tracef(int lvl, const char * __fmt, ...) 
	__attribute__ ((format (printf, 2, 3)));

/*****************************************************************************
 * Timer API
 *****************************************************************************/

void chime_tmr_init(int tmr_id, void (* isr)(void), 
					uint32_t timeout, uint32_t period);

void chime_tmr_reset(int tmr_id, uint32_t timeout, uint32_t period);

void chime_tmr_start(int tmr_id);

void chime_tmr_stop(int tmr_id);

uint32_t chime_tmr_count(int tmr_id);

/*****************************************************************************
 * Communications API
 *****************************************************************************/

int chime_comm_create(const char * name, struct comm_attr * attr);

int chime_comm_attach(int chan, const char * name, 
					  void (* rcv_isr)(void), void (* eot_isr)(void),
					  void (* dcd_isr)(void));

int chime_comm_write(int chan, const void * buf, size_t len);

int chime_comm_read(int chan, void * buf, size_t len);

int chime_comm_close(int chan);

/*****************************************************************************
 * Variables
 *****************************************************************************/

int chime_var_open(const char * name);

int chime_var_close(int oid);

bool chime_var_rec(int oid, double value);

/*****************************************************************************
 * Chime Server
 *****************************************************************************/

int chime_server_start(const char * ctrl_fifo);

int chime_server_stop(void);

void chime_server_info(FILE * f);

void chime_server_resume(void);

void chime_server_pause(void);

void chime_server_speed_set(float val);

void chime_server_reset(void);

struct trace_entry * chime_trace_get(void);

bool chime_trace_free(struct trace_entry * entry);

void chime_trace_dump(struct trace_entry * entry);

void chime_server_comm_stat(void);

void chime_server_var_dump(void);

int chime_trace_dump_start(void);

int chime_trace_dump_stop(void);

/*****************************************************************************
 * Chime Management
 *****************************************************************************/

#ifdef __cplusplus
}
#endif	

#endif /* __CHIME_H__ */


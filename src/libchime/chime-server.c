#include <errno.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>

#define __CHIME_I__
#include "chime-i.h"

#define __CLK_HEAP__
#include "clk-heap.h"

#include "objpool.h"
#include "list.h"

#define CHIME_NODE_BMP_LEN (((CHIME_NODE_MAX) + 63) / 64)

/*****************************************************************************
 * Server
 *****************************************************************************/

struct chime_server {
	bool started;
	__mq_t mq;
	volatile bool enabled;
	__mutex_t mutex;
	__thread_t ctrl_thread;

	struct {
		uint32_t checkout_cnt; /* track nodes running */
		uint32_t sid; /* session id */
		uint64_t period;
		uint64_t clk;
		uint32_t tick_cnt;
		uint32_t tick_lost;
		volatile bool paused;
	} sim;

	struct {
		__mq_t mq;
		uint64_t period;
		volatile uint32_t tick_cnt;
		volatile uint32_t req;
		volatile uint32_t ack;
	} tmr;

	struct clk_heap * heap;

	uint32_t probe_seq;

	float temperature;
	char mqname[PATH_MAX];

	uint64_t node_alloc_bmp[CHIME_NODE_BMP_LEN]; /* allocation bitmap */
	struct chime_node * node[CHIME_NODE_MAX + 1]; /* node set */
	uint8_t node_idx[CHIME_NODE_MAX + 1];

	uint16_t comm_oid[CHIME_COMM_MAX + 1]; /* list of comms by oid */
	uint16_t var_oid[CHIME_VAR_MAX + 1]; /* list of variables by oid */

	uint16_t shared_oid;
	struct srv_shared * shared;
};

static struct chime_server server = {
	.started = false,
	.enabled = false,
};

uint64_t __chime_clock(void)
{
	return server.heap->clk;
}

static struct chime_node * __node_getinstance(int node_id)
{
	struct chime_node * node;

	assert(node_id > 0);
	assert(node_id <= CHIME_NODE_MAX);

	node = server.node[node_id];

	assert(node != NULL);
	assert(node->id == node_id);

	return node;
}

const uint64_t bin_range[20] = {
	   1 * USEC,  
	   2 * USEC,  
	   5 * USEC,  
	  10 * USEC,  
	  20 * USEC,  
	  50 * USEC,  
	 100 * USEC,  
	 200 * USEC,  
	 500 * USEC,  
	   1 * MSEC,  
	   2 * MSEC,  
	   5 * MSEC,  
	  10 * MSEC,  
	  20 * MSEC,  
	  50 * MSEC,  
	 100 * MSEC,  
 	 200 * MSEC, 
	 500 * MSEC,
    1000 * MSEC,
    2000 * MSEC
};

void __chime_comm_reset(struct chime_comm * comm)
{
	struct chime_node * node;
	struct comm_attr * attr;
	uint32_t bits_max;
	uint64_t dt_max;
	uint64_t delay_max;
	uint64_t bin_width;
	uint32_t wr_cycles;
	int j;

	DBG("reseting comm \"%s\"...", comm->name);

	assert(comm != NULL);

	objpool_lock();

	/* get the comm attributes */
	attr = &comm->attr;

	/* clear statistic series bins */
	for (j = 0; j < COMM_STAT_BINS; ++j)
		comm->stat[j] = 0;
	comm->cnt = 0;

	dt_max = 0;
	for (j = 1; j < LIST_LEN(comm->node_lst); ++j) {
		node = __node_getinstance(comm->node_lst[j]);
		if (node->dt > dt_max)
			dt_max = node->dt;
	}

	comm->fix_delay = attr->min_delay * SEC;
	comm->bit_time = (1.0 / attr->speed_bps) * SEC;

	wr_cycles = attr->wr_cyc_per_byte * attr->bytes_max + attr->wr_cyc_overhead;
	bits_max = attr->bits_per_byte * attr->bytes_max + attr->bits_overhead;
	delay_max = (wr_cycles * dt_max) + comm->fix_delay + attr->max_jitter * SEC;
	delay_max += attr->nodes_max * attr->nod_delay;
	delay_max += bits_max * comm->bit_time;

	DBG("delay_max=%f", TS2F(delay_max));

	bin_width = (delay_max + COMM_STAT_BINS - 1) / COMM_STAT_BINS;

	/* find the best bin width */
	for (j = 0; j < 20; ++j) {
		if (bin_width <= bin_range[j]) {
			bin_width = bin_range[j];
			break;
		}
	}

	comm->bin_delay = bin_width;

	/* FIXME: initialize the seed from attribute */
	comm->randseed0 = 1000LL;
	comm->randseed1 = 1000000LL;

	/* initialize the exponential random generator */
	exp_rand_init(&comm->exprnd, 0.3, 1000000000000LL);
//	exp_rand_init(&comm->exprnd, 0.5, 1000000000LL);

	objpool_unlock();
}

bool __chime_comm_stat_dump(struct chime_comm * comm)
{
	char fname[ENTRY_NAME_MAX + 8];
	char plt_path[PATH_MAX];
	char dat_path[PATH_MAX];
	char out_path[PATH_MAX];
	double bin_width;
	double x;
	double x_max;
	double y;
	double y_max;
	FILE * f;
	int j;

	if (comm->attr.hist_en == false)
		return true;

	strncpy(fname, comm->name, ENTRY_NAME_MAX);
	fname[ENTRY_NAME_MAX] = '\0';

	sprintf(dat_path, "./%s.dat", fname);
	sprintf(plt_path, "./%s.plt", fname);
	sprintf(out_path, "./%s.png", fname);

	if ((f = fopen(dat_path, "w")) == NULL) {
		ERR("fopen(\"%s\") failed: %s!", dat_path, __strerr());
		return false;
	}

	/* Find the maximum values */
	y_max = 0;
	for (j = 0; j < COMM_STAT_BINS; ++j) {
		y = ((double)comm->stat[j] / (double)comm->cnt);
		if (y > y_max)
			y_max = y;
	}
	bin_width = TS2F(comm->bin_delay);
	x_max = bin_width * COMM_STAT_BINS;

	for (j = 0; j < COMM_STAT_BINS; ++j) {
		uint32_t n = comm->stat[j];
		x = j * bin_width;
		y = ((double)n / (double)comm->cnt);
		if (n > 0) {
			fprintf(f, "%10.8f %10.8f\n", x, y / y_max);
		}
	}

	fclose(f);

	if ((f = fopen(plt_path, "w")) == NULL) {
		ERR("fopen(\"%s\") failed: %s!", plt_path, __strerr());
		return false;
	}
	fprintf(f, "min=0\n");
	fprintf(f, "max=%0.9f\n", x_max);
	fprintf(f, "width=%0.9f\n", bin_width);
	fprintf(f, "set terminal png size 1280,768 font 'Verdana,10'\n");
	fprintf(f, "set output '%s'\n", out_path);
	fprintf(f, "set xrange [0:max]\n");
	fprintf(f, "set yrange [0:%0.9f]\n", 1.0);
	fprintf(f, "set offset graph 0.05,0.05,0.05,0.0\n");
	fprintf(f, "set boxwidth width\n");
	fprintf(f, "set style fill solid 0.5\n");
	fprintf(f, "set tics out nomirror\n");
	fprintf(f, "set xlabel 'Delay(s)'\n");
	fprintf(f, "set ylabel 'Frequency'\n");
	fprintf(f, "plot '%s' using 1:2 smooth freq "
			"with boxes lc rgb'blue' notitle\n", dat_path);
	fprintf(f, "set output\n");
	fprintf(f, "quit\n");
	fclose(f);

	return true;
}

static void __chime_comm_hist_add(struct chime_comm * comm, uint64_t delay)
{
	int n;

	DBG5("bin_delay=%"PRIu64" delay=%"PRIu64".", 
		TS2USEC(comm->bin_delay), TS2USEC(delay));

	n = delay / comm->bin_delay;
	assert(n < COMM_STAT_BINS);

	comm->stat[n]++;
	comm->cnt++;
}

bool __chime_var_flush(struct chime_var * var)
{
	int i;

	if (var->f_dat == NULL) {
		char dat_path[PATH_MAX];

		sprintf(dat_path, "./%s.dat", var->name);

		if ((var->f_dat = fopen(dat_path, "w")) == NULL) {
			ERR("fopen(\"%s\") failed: %s!", dat_path, __strerr());
			return false;
		}
	}
	
	if (var->pos > var->cnt) {
		ERR("var->pos(%d) <= var->cnt(%d)!", var->pos, var->cnt);
		assert(var->pos <= var->cnt);
	}

	if (var->pos == 0) {
		rewind(var->f_dat);
	}

	for (i = var->pos; i < var->cnt; ++i) {
		struct var_rec * rec = &var->rec[i];
		fprintf(var->f_dat, "%.9f, %.9f\n", rec->t, rec->y);
	}

	fflush(var->f_dat);

	var->pos = i;

	return true;
}

static void __chime_var_reset(struct chime_var * var)
{
	assert(var != NULL);
	objpool_lock();
	var->clk = server.sim.clk;
	var->cnt = 0;
	var->pos = 0;
	objpool_unlock();

	if (var->f_dat != NULL) {
		fclose(var->f_dat);
		var->f_dat = NULL;
	}
}


bool __chime_var_dump(struct chime_var * var)
{
	char name[ENTRY_NAME_MAX + 8];
	char plt_path[PATH_MAX];
	char out_path[PATH_MAX];
	char dat_path[PATH_MAX];
	FILE * f;

	strncpy(name, var->name, ENTRY_NAME_MAX);
	name[ENTRY_NAME_MAX] = '\0';

	sprintf(plt_path, "./%s.plt", name);
	sprintf(out_path, "./%s.png", name);
	sprintf(dat_path, "./%s.dat", name);

	if ((f = fopen(plt_path, "w")) == NULL) {
		ERR("fopen(\"%s\") failed: %s!", plt_path, __strerr());
		return false;
	}
	fprintf(f, "#set terminal wxt\n");
	fprintf(f, "set terminal png size 1280,768 font 'Verdana,12'\n");
	fprintf(f, "set output '%s'\n", out_path);
	fprintf(f, "set offset graph 0.0,0.0,0.0,0.0\n");
	fprintf(f, "# labels\n"); /* labels */
	fprintf(f, "set xlabel 'Time(s)'\n");
	fprintf(f, "set ylabel '%s'\n", name);
	fprintf(f, "# define axis\n"); /* axis */
	fprintf(f, "set style line 11 lc rgb '#808080' lt 1\n");
	fprintf(f, "set border 3 back ls 11\n");
	fprintf(f, "set tics out nomirror\n");
	fprintf(f, "# define grid\n"); /* grid */
	fprintf(f, "set style line 12 lc rgb '#808080' lt 0 lw 1\n");
	fprintf(f, "set grid back ls 12\n");
	fprintf(f, "# line styles\n"); /* line styles */
	fprintf(f, "set style line 1 lc rgb '#801010' pt 0 lt 1 lw 2\n");
	fprintf(f, "set style line 2 lc rgb '#108010' pt 0 lt 1 lw 2\n");
	fprintf(f, "set style line 3 lc rgb '#101080' pt 0 lt 1 lw 2\n");
	fprintf(f, "plot '%s' using 1:2 notitle with lp ls 1\n", dat_path);
	fprintf(f, "set output\n");
	fprintf(f, "quit\n");
	fclose(f);

	return true;
}

static double xtal_temp_offs(float tc, float t)
{
	return 1.0 - tc * (t - 25.0) * (t - 25.0);
}

static void __chime_node_alloc_init(void)
{
	int id;

	__bmp_alloc_init(server.node_alloc_bmp, CHIME_NODE_BMP_LEN);
	/* The first node_id (0) is invalid, reserved it by allocating.. */
	id = __bmp_bit_alloc(server.node_alloc_bmp, CHIME_NODE_BMP_LEN);
	assert(id == 0);
	(void)id;
}

static int __chime_node_alloc(void)
{
	return __bmp_bit_alloc(server.node_alloc_bmp, CHIME_NODE_BMP_LEN);
}

static void __chime_node_free(int id)
{
	__bmp_bit_free(server.node_alloc_bmp, CHIME_NODE_BMP_LEN, id);
}

/* Clear events targeted to node_id */
static int __chime_node_clear_events(int node_id)
{
	struct chime_event evt;
	int i;
	int n;

	/* remove pending events !!! */
	i = 1;
	n = 0;
	while (heap_pick(server.heap, i, NULL, &evt)) {
		if (evt.node_id == node_id) {
			DBG("<%d> deleting event %s", evt.node_id, __evt_opc_nm[evt.opc]);
			heap_delete(server.heap, i);
			if (evt.opc == CHIME_EVT_RCV) {
				DBG("<%d> releasing object OID=%d", evt.node_id, evt.buf.oid);
				obj_release(evt.buf.oid);
			}
			n++;
		} else {
			i++;
		}
	}

	if (n > 0) {
		DBG("<%d> %d events deleted", node_id, n);
	}

	return n;
}

/* Remove node_id from all comms */
static int __chime_node_clear_comms(int node_id)
{
	int n = 0;
	int i;

	/* remove from comm lists !!! */
	for (i = 1; i <= LIST_LEN(server.comm_oid); ++i) {
		struct chime_comm * comm;

		comm = obj_getinstance(server.comm_oid[i]);
		objpool_lock();
		if (u8_list_contains(comm->node_lst, node_id)) {
			INF("<%d> removing from COMM %d", node_id, server.comm_oid[i]);
			u8_list_remove(comm->node_lst, node_id);
			n++;
		}
		objpool_unlock();
	}

	if (n > 0) {
		DBG("<%d> %d COMMs cleared", node_id, n);
	}

	return n;
}

/* remove a node from simulation 
   return the state of the breakpoint flag  
 */
static bool __chime_node_remove(int node_id)
{
	struct chime_node * node;
	bool bkpt;

	node = __node_getinstance(node_id);

	bkpt = node->bkpt;

	INF("removing node: %d, oid=%d.", node_id, obj_oid(node));

	/* remove from vector of nodes */
	server.node[node_id] = NULL;

	/* close event message queue */
	__mq_close(node->s.evt_mq);
	/* decrement the object's reference count */
	obj_decref(node);
	/* remove from the node index */
	u8_list_remove(server.node_idx, node_id);
	/* free the node ID */
	__chime_node_free(node_id);

//	heap_dump(stderr, server.heap);
	__chime_node_clear_events(node_id);

	__chime_node_clear_comms(node_id);
	
	return bkpt;
}

static bool __chime_node_reset(int node_id, uint32_t sid)
{
	struct chime_event evt;
	struct chime_node * node;

	node = __node_getinstance(node_id);

	/* restart clock */
	node->clk = server.heap->clk;
	/* restart time */
	node->time = 0;

	/* send a reset event to the node */
	evt.node_id = node->id;
	evt.oid = 0;
	evt.opc = CHIME_EVT_RESET;
	evt.sid = sid;

	DBG("<%d> reset...", node_id);

	if (__mq_send(node->s.evt_mq, &evt, CHIME_EVENT_LEN) < 0) {
		WARN("<%d> __mq_send() failed!", evt.node_id);
		return false;
	}

	node->bkpt = false;
	/* update the simulation running count */
	server.sim.checkout_cnt++;

	return true;
}

/* Node probing:
   1. the server sends an event with a sequence number to all nodes.
   2. the node write the sequence into it's shared node block 
   3. server waits 50ms
   4. server compare the node's shared memory sequence
 */

static void __chime_sanity_check(void)
{
	uint8_t err[CHIME_NODE_MAX + 1];
	struct chime_event evt;
	int i;

	if (LIST_LEN(server.node_idx) == 0) {
		/* no nodes on the list, just return */
		return;
	}

	/* initialize list of errors */
	u8_list_init(err);

	/* prepare the event structure */
	evt.opc = CHIME_EVT_PROBE;
	evt.oid = 0;
	/* update the probe sequence */
	evt.seq = server.probe_seq += 1000000;

	DBG1("sending probe events...");
	for (i = 1; i <= LIST_LEN(server.node_idx); ++i) {
		int node_id = server.node_idx[i];
		struct chime_node * node = __node_getinstance(node_id);

		INF("<%d> probing... bkpt=%d", node_id, node->bkpt);

		/* clear node probe sequence */
		node->probe_seq = 0;

		evt.node_id = node_id;
		__mq_send(node->s.evt_mq, &evt, CHIME_EVENT_LEN);
	}

	__msleep(50);

	DBG1("checking if nodes are running...");
	for (i = 1; i <= LIST_LEN(server.node_idx); ++i) {
		int node_id = server.node_idx[i];
		struct chime_node * node = __node_getinstance(node_id);

		if (node->probe_seq != evt.seq) {
			/* insert on the error list */
			u8_list_insert(err, node_id);
		}

		INF("<%d> checking... bkpt=%d", node_id, node->bkpt);
	}

	if (LIST_LEN(err) > 0) {
		DBG("removing dead nodes...");
		for (i = 1; i <= LIST_LEN(err); ++i) {
			if (!__chime_node_remove(err[i])) {
				/* node was running (bkpt = false) */
				/* decrement the node run count */
				server.sim.checkout_cnt--;
			}
		}
	}
}

/*****************************************************************************
 * Simulation
 *****************************************************************************/

static void __sim_timer_isr(void)
{
	struct chime_req_hdr req;
	register uint32_t ack;

	server.tmr.tick_cnt++;

	DBG5("tick");

	/* check for pending timer notification requests */
	if (server.tmr.ack != (ack = server.tmr.req)) {
		server.tmr.ack = ack;

		DBG3("ticks=%u.", server.tmr.tick_cnt);

		req.node_id = 0;
		req.opc = CHIME_SIG_SIM_TICK;
		req.oid = 0;
		if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0) {
			ERR("__mq_send() failed: %s.", __strerr());
		}
	}
}

static void __sim_timer_reset(void)
{
	DBG("consuming simulation ticks...");
	/* consume all timer ticks */
	server.sim.tick_cnt = server.tmr.tick_cnt;
	/* reset lost ticks */
	server.sim.tick_lost = 0;
	/* reset the simulation time budget */
	server.sim.clk = server.heap->clk;
}

static void __chime_sim_reset(void)
{
	struct chime_event evt;

	/* FIXME: pause timer???? */
	server.tmr.ack = 0;
	server.tmr.req = 0;
	server.tmr.tick_cnt = 0;

	/* restarting simulation time */
	server.shared->time = 0;

	if (server.sim.paused)
		server.sim.checkout_cnt = 1;
	else
		server.sim.checkout_cnt = 0;

	INF("clearing clock heap!");
	/* get all events from the heap */
	while (heap_extract_min(server.heap, NULL, &evt)) {
		if (evt.oid != 0) {
			DBG1("releasing object OID=%d node_id=%d event=%s", 
				 evt.node_id, evt.oid, __evt_opc_nm[evt.opc]);
			obj_release(evt.oid);
		}
	}

	server.heap->clk = 0LL;

	__sim_timer_reset();
}

#define CHIME_TICKS_PER_STEP_MAX 4

/* This is the simulation dispatcher... */

static void __chime_sim_step(void)
{
	struct chime_event evt;
	uint64_t sim_clk; /* simulation budget clock */
	uint64_t max_clk; /* step window clock */
	uint64_t cpu_clk; /* cpu clock */

	/* get the first clock from the heap */
	if (!heap_minimum(server.heap, &cpu_clk, &evt)) {
		WARN("clock heap is empty!!!");
		return;
	}

	/* get the simulation timer clock */
	sim_clk = server.sim.clk;
	DBG4("sim_clk=%"PRId64".", sim_clk);

	/* if (sim_clk <= cpu_clk) */
	while ((int64_t)(sim_clk - cpu_clk) <= 0) {
		int32_t ticks;
		/* Our simulation time bugdet is exhausted. Check if we have some
		   timer ticks available for consumption now...  */

		/* get available timer ticks */
		ticks = (int32_t)(server.tmr.tick_cnt - server.sim.tick_cnt);
		if (ticks == 0) {
			/* We are running fast, wait for the timer to catch up.  */
			DBG1("need ticks: %"PRIu64, TS2USEC(-(int64_t)(sim_clk - cpu_clk)));
			server.tmr.req++; /* request timer notification */
			return; /* wait for timer notification */
		}

		/* consume the ticks */
		server.sim.tick_cnt += ticks;

		if (ticks > CHIME_TICKS_PER_STEP_MAX) {
			/* The simulation is running slow.
			   it is time to get a faster computer... */ 
			if (server.sim.tick_lost == 0) {
				WARN("sluggishness detected...");
			}
			/* limit the simulation ticks consumption, 
			   to avoid it growing fat. Add the remaining ticks
			   to the lost count for speed correction. */
			server.sim.tick_lost += ticks - CHIME_TICKS_PER_STEP_MAX;
			ticks = CHIME_TICKS_PER_STEP_MAX;
		};

		/* update the simulation budget clock */
		sim_clk = server.sim.clk += (int64_t)ticks * server.sim.period;
		DBG1("ticks=%d sim.clk=%"PRId64".", ticks, server.sim.clk);
	}

	/* set the initial step clock to the simulation budget */
	max_clk = sim_clk;

	DBG3("max_clk=%"PRIu64" --------", max_clk);

	/* Parallel run decision algorithm */

	/* Assumptions:
	   - A node runs at least one CPU cycle

	1. The next CPU to run is the one with the clock
	 closer to the simulation clock ... */

	do {
		int node_id = evt.node_id;
		struct chime_node * node = server.node[node_id];
		uint64_t clk;
		int64_t dt;
		uint32_t cycles;

//	heap_dump(stderr, server.heap);

		/* dead node !!!! */
		assert(node != NULL); 

		/* FIXME: Is it possible to have multiple events to the same node
		   (CPU) in the event heap.
		   We keep track of this by means of the breakpoint
		   indication flag (bkpt).
		   When the CPU checks-in for simulation, the breakpoint
		   flag is set.
		   When the first event is dispatched the breakpoint
		   flag is cleared. */
		if (!node->bkpt) {
			/* If we reached this bracnh it means we have multiple events.
			   To simplify the client, we allow only one event to be
			   dispatched per node at each simulation step.
			   Stop the simulation after dispatching one event to the
			   node. */
			break;
		}

		/* remove the clock from the heap */
		heap_delete_min(server.heap);
		/* update the heap clock with the heap's head */
		server.heap->clk = cpu_clk;

		/* time elapsed time since last event */
		dt = (int64_t)(cpu_clk - node->clk);
		/* update the node clock */
		node->clk = cpu_clk;

		DBG1("<%d> node->clk=%"PRIu64" dt=%"PRId64".", 
			 node_id, node->clk, dt);

		assert(dt >= 0);

		/* update node ticks... */
		cycles = dt / node->dt;
		node->ticks += cycles;
		node->time += cycles * node->period;
		DBG3("<%d> cycles=%d bkpt=%d", node_id, cycles, node->bkpt);

		/* update the max clock step with the smallest
		   from the current step and the next possible
		   clock time (next cycle) from the current
		   CPU */

		/* get only nodes whose timeout is less then
		   this node's clock period */
		clk = cpu_clk + node->dt;

		/* if (clk < max_clk) */
		if ((int64_t)(clk - max_clk) < 0) {
			max_clk = clk;
			DBG3("max_clk=%"PRIu64, max_clk);
		}

		/* Multiple events to the same node (CPU) are possible.
		   We keep track of this by means of the breakpoint
		   indication flag.
		   When the CPU checks in for simulation, the breakpoint
		   flag is set.
		   When the first event is dispatched the breakpoint
		   flag is cleared. The number of running CPUs is updated
		   accordingly. */
		node->bkpt = false; /* clear breakpoint flag */
		/* update the running count */
		server.sim.checkout_cnt++;
		DBG3("server.sim.checkout_cnt=%d.", server.sim.checkout_cnt );

#if 0
		if (evt.opc == CHIME_EVT_EOT0) {
			DBG("<%d> COMM{oid=%d} EOT0.", evt.node_id, evt.oid);
		} else if (evt.opc == CHIME_EVT_RCV) {
			DBG("<%d> COMM{oid=%d} buf{oid=%d len=%d} RCV.", evt.node_id, 
				evt.oid, evt.buf.oid, evt.buf.len);
		}
#endif

		DBG3("<%d> [%s]", node_id, __evt_opc_nm[evt.opc]);

		if (__mq_send(node->s.evt_mq, &evt, CHIME_EVENT_LEN) < 0) {
			WARN("<%d> __mq_send() failed!", node_id);
			/* remove unresponsive node... */
			__chime_node_remove(node_id);
		}

		/* get the next clock from the heap */
		if (!heap_minimum(server.heap, &cpu_clk, &evt)) {
			DBG1("heap empty...");
			break;
		}

		/* if (cpu_clk < max_clk) */
	} while ((int64_t)(cpu_clk - max_clk) < 0);

	/* done. wait for next sync... */
	DBG3("done.");
};

void __chime_sig_pause_sim(struct chime_request * req)
{
	if (!server.sim.paused) {
		/* clear timer ticks requests */
		server.tmr.ack = server.tmr.req;

		/* artificially increase the number of running CPUs 
		   to prevent simulation stepping */
		server.sim.checkout_cnt++;

		/* set the paused flag */
		server.sim.paused = true;
	}
}

void __chime_sig_resume_sim(struct chime_request * req)
{
	if (server.sim.paused) {
		server.sim.paused = false;
		__chime_sanity_check();
		/* reset simulation timer */
		__sim_timer_reset();
		/* step the simulation */
		server.sim.checkout_cnt--;
		if (server.sim.checkout_cnt == 0)
			__chime_sim_step();
	}
}

void __chime_sig_sim_tick(struct chime_request * req)
{
	/* The timer clock is chimming, there are some 
	 ticks available ... */

	if (server.sim.checkout_cnt == 0)
		__chime_sim_step();
	else
		WARN("server.sim.checkout_cnt=%d", server.sim.checkout_cnt);
}

void __chime_req_bkpt(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node = server.node[node_id];

	if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
//   		assert(node != NULL);
		return;
	}

	if (node->sid != server.sim.sid) {
		WARN("<%d> wrong SID.", node_id);
		return;
	}

	assert(node->bkpt == false);
	node->bkpt = true; /* set breakpoint flag */

	DBG1("<%d> node->clk=%"PRIu64".", node_id, node->clk);

	/* decrement the node run count */
	server.sim.checkout_cnt--;
	if (server.sim.checkout_cnt == 0) {
		DBG2("<%d> SYN.", node_id);
		/* all CPUs checked in, step the simulator */
		__chime_sim_step();
	} else {
		DBG2("<%d> checkout_cnt=%d ...", node_id, server.sim.checkout_cnt);
	}
}

void __chime_req_timer(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node = server.node[node_id];
	uint32_t cycles = req->timer.ticks;
	struct chime_event evt;
	uint64_t clk;

	if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
//  		assert(node != NULL);
		return;
	}

	if (node->sid != server.sim.sid) {
		WARN("<%d> wrong SID.", node_id);
		return;
	}

	if (cycles == 0) {
		WARN("<%d> cycles == 0 !!!", node_id);
		assert(cycles > 0);
		return;
	}

	evt.node_id = node_id;
	evt.opc = CHIME_EVT_TMR0 + req->opc - CHIME_REQ_TMR0;
	evt.oid = req->oid;
	evt.ticks = cycles;
	evt.seq = req->timer.seq;

	DBG1("<%d> node->clk=%"PRIu64".", node_id, node->clk);

	/* next time event */
	clk = node->clk + (node->dt * cycles);

#if 0
	if ((int64_t)(clk - server.heap->clk) < 0) {
		WARN("<%d> cycles=%u node->clk=%"PRIu64".", node_id, cycles, node->clk);
		WARN("clk=%"PRIu64" heap->clk=%"PRIu64" diff=%"PRId64"", 
			 clk, server.heap->clk, (int64_t)(clk - server.heap->clk));
	}
#endif

	/* insert into the clock simulation heap */
	assert((int64_t)(clk - server.heap->clk) >= 0);
	heap_insert_min(server.heap, clk, &evt);
}

void __chime_req_step(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node = server.node[node_id];
	uint32_t cycles = req->step.cycles;
	struct chime_event evt;
	uint64_t clk;

	if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
   		assert(node != NULL);
		return;
	}

	if (node->sid != server.sim.sid) {
		WARN("<%d> wrong SID.", node_id);
		return;
	}

	if (cycles == 0) {
		DBG("<%d> cycles == 0 !!!", node_id);
//		assert(cycles > 0);
		return;
	}

	/* next time event */
	clk = node->clk + (node->dt * cycles);

	DBG1("<%d> node->clk=%"PRIu64".", node_id, node->clk);

	assert(node->bkpt == false);
	node->bkpt = true; /* set breakpoint flag */

	evt.node_id = node_id;
	evt.opc = CHIME_EVT_STEP;
	evt.oid = 0;
	evt.ticks = node->ticks + cycles;

#if DEBUG
	/* insert into the clock simulation heap */
	if ((int64_t)(clk - server.heap->clk) < 0) {
		WARN("<%d> clk=%"PRIu64" diff=%"PRId64, node_id, clk, 
			 (int64_t)(clk - server.heap->clk));
		WARN("<%d> cycles=%d", node_id, cycles);
		WARN("<%d> node->clk=%"PRIu64" diff=%"PRId64, node_id, node->clk, 
			 (int64_t)(node->clk - server.heap->clk));
		heap_dump(stderr, server.heap);
	}
#endif

	assert((int64_t)(clk - server.heap->clk) >= 0);
	heap_insert_min(server.heap, clk, &evt);

	/* decrement the node run count */
	server.sim.checkout_cnt--;
	if (server.sim.checkout_cnt == 0) {
		DBG2("<%d> cycles=%d clk=%"PRIu64" SYN.", node_id, cycles, clk);
		/* all CPUs checked in, step the simulator */
		__chime_sim_step();
	} else {
		DBG5("<%d> cycles=%d clk=%"PRIu64".", node_id, cycles, clk);
		DBG2("<%d> checkout_cnt=%d ...", node_id, server.sim.checkout_cnt);
	}
}

void __chime_req_comm_xmt(struct chime_request * req)
{
	int xmt_id = req->node_id;
	int oid = req->oid;
	struct chime_node * xmt_node;
	struct chime_event evt;
	struct chime_comm * comm;
	struct comm_attr * attr;
	uint32_t wr_cycles;
	uint32_t bits;
	uint64_t rcv_clk;
	uint64_t eot_clk;
	uint32_t rd_cycles;
	uint64_t mac_delay;
	uint64_t xmt_delay;
	uint64_t propagation_delay;
	uint64_t xmt_dt;
	uint64_t clk;
	void * buf;
	int len;
	int i;

	/* get the transmitting node instance */
	xmt_node = server.node[xmt_id];
	if (xmt_node == NULL) {
		WARN("<%d> invalid node!!!", xmt_id);
   	//	assert(xmt_node != NULL);
		return;
	}

	if (xmt_node->sid != server.sim.sid) {
		WARN("<%d> wrong SID.", xmt_id);
		return;
	}

	/* get the associated object */
	comm = obj_getinstance_incref(oid);
	/* get the comm attributes */
	attr = &comm->attr;

	DBG1("<%d> COMM{oid=%d} buf{oid=%d len=%d}.", xmt_id, oid,
		 req->comm.buf_oid, req->comm.buf_len);

	/* frame info */
	buf = obj_getinstance(req->comm.buf_oid);
	len = req->comm.buf_len;

	evt.node_id = xmt_id;
	evt.oid = oid;
	evt.u32 = 0;

	/* number of CPU cycles required for the node to write the message */
	wr_cycles = attr->wr_cyc_per_byte * len + attr->wr_cyc_overhead;

	/* number of bits in the frame */
	bits = attr->bits_per_byte * len + attr->bits_overhead;
	/* transmission time */
	xmt_dt = bits * comm->bit_time;

	DBG4("bits=%u speed=%0.1fbps xmt_dt=%"PRIu64"", 
		bits, attr->speed_bps, TS2USEC(xmt_dt));

	/* Medium access delay */
	if (attr->max_jitter != 0) {
		double latency;
		/* low order approximation of a normal distribution random number,
		   using the central limit theorem.
		   The number is distributed over the interval 0..1 and
		   centered at 0.5 */
		if (attr->exp_en)
			latency = attr->max_jitter * exp_rand(&comm->exprnd);
		else
			latency = attr->max_jitter * norm_rand(&comm->randseed0);
		mac_delay = comm->fix_delay + latency * SEC;
		DBG4("latency=%0.6f delay=%"PRIu64".", latency, TS2USEC(mac_delay));
	} else {
		mac_delay = comm->fix_delay;
		DBG4("latency=0.0 delay=%"PRIu64".", TS2USEC(mac_delay));
	}
	
	if (attr->nod_delay != 0) {
		int n = LIST_LEN(comm->node_lst);
		mac_delay += unif_rand(&comm->randseed1) * attr->nod_delay * n * SEC;
	}

	mac_delay += wr_cycles * xmt_node->dt;

	/* total transmission time */
	xmt_delay = mac_delay + xmt_dt;

	if (attr->hist_en) {
		/* update statistics */
		__chime_comm_hist_add(comm, xmt_delay);
	}

	/* absolute clock time for end of transmission */
	eot_clk = xmt_node->clk + xmt_delay;

	if (attr->txbuf_en) {
		/* buffer write clock */
		clk = xmt_node->clk + (wr_cycles * xmt_node->dt);
	} else {
		uint32_t eot_cycles;
		/* cycles round down (floor) */
		eot_cycles = (eot_clk - xmt_node->clk) / xmt_node->dt; 
		clk = xmt_node->clk + (xmt_node->dt * eot_cycles);
	}

	/* insert EOT event into queue */
	evt.opc = CHIME_EVT_EOT0 + req->opc - CHIME_REQ_XMT0;
	assert((int64_t)(clk - server.heap->clk) >= 0);
	heap_insert_min(server.heap, clk, &evt);

	/* number of CPU cycles required for the node to read the message */
	rd_cycles = attr->rd_cyc_per_byte * len + attr->rd_cyc_overhead;

	/* FIXME: propagation delay */
	propagation_delay = 0;
	/* absolute clock time for end of reception */
	rcv_clk = eot_clk + propagation_delay;
	/* insert one receive event for each node in the list */
	evt.buf.oid = req->comm.buf_oid;
	evt.buf.len = len;
	for (i = 1; i <= LIST_LEN(comm->node_lst); ++i) {
		struct chime_node * node;
		uint32_t rcv_cycles;
		uint32_t cycles;
		int id;


		id = comm->node_lst[i];
		if (id == xmt_id) /* don't send back to the transmitter */
			continue;

		DBG3("<%d> --> <%d>", xmt_id, id);

		node = server.node[id];
		if (node == NULL) {
			WARN("<%d> invalid node!!!", id);
   			assert(node != NULL);
			continue;
		}	

		evt.node_id = id;

		if (attr->dcd_en) {
			uint64_t dcd_clk;

			/* absolute clock time for data carrier detection,
			   DCD is detected after 1 bit */
			dcd_clk = xmt_node->clk + mac_delay + comm->bit_time;
			cycles = (dcd_clk - node->clk + node->dt - 1) / node->dt;
			cycles += attr->rd_cyc_overhead;
			clk = node->clk + (uint64_t)node->dt * cycles;
			/* insert DCD event into queue */
			evt.opc = CHIME_EVT_DCD;
			assert((int64_t)(clk - server.heap->clk) >= 0);
			heap_insert_min(server.heap, clk, &evt);
		}

		/* Round up the number of cycles for this node to receive and
		   read the comm data.
		   The rcv_clk is the absolute time for the end of reception.
		  The absolute time must be converted to the CPU time. */
		/* round up (ceil) */
		rcv_cycles = (rcv_clk - node->clk + node->dt - 1) / node->dt;
		cycles = rd_cycles + rcv_cycles;
		clk = node->clk + (uint64_t)node->dt * cycles;

#if 0
		INF("node_clk=%"PRIu64" wr_clk=%"PRIu64
				" rcv_clk=%"PRIu64".\n",
				__func__, TS2USEC(node->clk),
				TS2USEC(wr_clk), TS2USEC(rcv_clk));
		INF("wr_cycles=%u rd_cycles=%u rcv_cycles=%u.\n",
				__func__, wr_cycles, rd_cycles, rcv_cycles);
		INF("cycles=%u clk=%"PRIu64".\n",
				__func__, cycles, TS2USEC(clk));
#endif

		/* increment object reference counter */
		obj_incref(buf);
		/* insert RCV event into queue */
		evt.opc = CHIME_EVT_RCV;
		assert((int64_t)(clk - server.heap->clk) >= 0);
		heap_insert_min(server.heap, clk, &evt);
	}

	obj_decref(buf);
	obj_decref(comm);

//	if (xmt_id == 1) || (oid < 11)
//	if (oid < 11)
//		heap_dump(stderr, server.heap);
}

void __chime_req_cpu_halt(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node;
//	bool bkpt;

	/* sanity check */
    if ((node = server.node[node_id]) == NULL) {
		WARN("<%d> invalid node!!!", node_id);
		return;
	}

	assert(node->bkpt == false);

//	bkpt = node->bkpt;
	INF("<%d> CPU halted!", node_id);

	__chime_node_clear_events(node_id);

	__chime_node_clear_comms(node_id);

//	heap_dump(stderr, server.heap);
	__chime_node_clear_events(node_id);

	__chime_node_clear_comms(node_id);
}

void __chime_req_bye(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node;

	/* sanity check */
    if ((node = server.node[node_id]) == NULL) {
		WARN("<%d> invalid node!!!", node_id);
		return;
	}

	INF("<%d> oid=%d, bye-bye!", node_id, req->oid);

	if (!__chime_node_remove(node_id)) {
		/* decrement the node run count */
		if (--server.sim.checkout_cnt == 0) {

			/* all CPUs checked in, step the simulator */
			__chime_sim_step();
		} else { 
			DBG2("<%d> checkout_cnt=%d ...", node_id, server.sim.checkout_cnt);
		}
	}

}

void __chime_req_abort(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node = server.node[node_id];

	/* sanity check */
    if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
		assert(node != NULL);
		return;
	}

	if (node->sid != server.sim.sid) {
		WARN("<%d> wrong SID.", node_id);
		return;
	}

	assert(node->bkpt == false);

	WARN("RIP. <%d> OID=%d, died with code %d.", 
		node_id, req->oid, req->abort.code);
	__chime_node_remove(node_id);

	/* decrement the node run count */
	if (--server.sim.checkout_cnt == 0) {
		/* all CPUs checked in, step the simulator */
		__chime_sim_step();
	} else { 
		DBG2("<%d> checkout_cnt=%d ...", node_id, server.sim.checkout_cnt);
	}
}


/*****************************************************************************
 * (RPC) Remote requests
 *****************************************************************************/

void __chime_node_join(struct chime_request * req)
{
	int oid = req->oid;
	struct chime_node * node;
	struct chime_event evt;
	int node_id;
	char * name;
	double offs_t; /* temperature offset */
	__mq_t mq;
	int ret;

	__chime_sanity_check();

	/* get the associated object */
	node = obj_getinstance_incref(oid);
	name = node->name;

	/* open the client message queue */
	if (__mq_open(&mq, name) < 0) {
		ERR("__mq_open(\"%s\") failed: %s.", name, __strerr());
		return;
	}

	/* try to allocate a node id */
	node_id  = __chime_node_alloc();
	INF("CPU=\"%s\" node_id=%d.", name, node_id);

	if (node_id < 0) {
		/* decrement object reference count */
		obj_decref(node);
		/* Couldn't allocate a new node, kick the client out... */
		evt.node_id = 0;
		evt.opc = CHIME_EVT_KICK_OUT;
	} else {
		/* sanity check */
		assert(server.node[node_id] == NULL);

		/* update operational variables */
		node->s.evt_mq = mq;
		node->id = node_id;
		node->clk = server.heap->clk;
		node->temperature = server.temperature;
		node->tc = (node->tc_ppm / 1000000.0);

		node->dres = ((1000000.0 + node->offs_ppm) * 1000);
		offs_t = xtal_temp_offs(node->tc, node->temperature);
		node->dt = node->dres * offs_t;
		node->period = (double)node->dt / (double)SEC;
		node->time = 0;

#if DEBUG
		{
			double freq_hz;

			freq_hz = node->dt / 1000.0;
			INF("offset=%.3fppm tc=%.3f ppm",
				node->offs_ppm, node->tc * 1000000);
			INF("dt=%" PRIu64 "(fs/us) f=%0.3fHz to=%f",
				node->dt, freq_hz, offs_t);
		}
#endif
		/* return the ID code */
		evt.node_id = node_id;
		evt.opc = CHIME_EVT_JOIN;
	}

	/* send an event, notifying the join completion */
	evt.oid = server.shared_oid;
	/* initial session id */
	evt.sid = server.sim.sid;

	if ((ret = __mq_send(mq, &evt, CHIME_EVENT_LEN)) < 0) {
		__mq_close(mq);
		WARN("mq_send() failed: %s.", __strerr());
		/* decrement object reference count */
		obj_decref(node);
		/* release the node strcuture */
		__chime_node_free(node_id);
	} else if (node_id >= 0) {
		/* insert in the node pointer list */
		server.node[node_id] = node;
		/* insert ID in the index list */
		u8_list_insert(server.node_idx, node_id);
		server.sim.checkout_cnt++;
		INF("checkout_cnt=%d.", server.sim.checkout_cnt);
	}

}

#define EVENT_PER_NODE_MAX 2048

void __chime_req_temp_set(struct chime_request * req)
{
	struct chime_event evt[EVENT_PER_NODE_MAX];
	uint64_t clk[EVENT_PER_NODE_MAX];
	int node_id = req->node_id;
	float t = req->temp.val;
	struct chime_node * node;
	double old_period;
	double old_dt;
	int i;
	int n;

	/* sanity check */
	node = server.node[node_id];
    if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
		assert(node != NULL);
		return;
	}

	old_dt = node->dt;
	(void)old_dt;
	old_period = node->period;
	(void)old_period;

	node->temperature = t;
	node->dt = node->dres * xtal_temp_offs(node->tc, node->temperature);
	node->period = (double)node->dt / (double)SEC;

	/* update clock on pending events !!! */
	i = 1;
	n = 0;
	while (heap_pick(server.heap, i, &clk[n], &evt[n])) {
		if (evt[n].node_id == node_id) {
			DBG2("<%d> updating event %s", node_id, __evt_opc_nm[evt[n].opc]);
			heap_delete(server.heap, i);
			n++;
			if (n == EVENT_PER_NODE_MAX) {
				ERR("<%d> events per node limit!", node_id);
				assert(0);
				break;
			}
		} else {
			i++;
		}
	}

	for (i = 0; i < n; ++i) {
		int32_t cycles;

		cycles = (clk[i] - node->clk) / old_dt;

		/* update evnt clock */
		clk[i] = node->clk + (node->dt * cycles);

		/* insert into the clock simulation heap */
		assert((int64_t)(clk[i] - server.heap->clk) >= 0);
		heap_insert_min(server.heap, clk[i], &evt[i]);
	}

	DBG3("<%d> %d events updated", node_id, n);

#if DEBUG
	{
		double freq_hz;
		freq_hz = node->dt / 1000.0;
		DBG1("temp=%.1f dt=%" PRIu64 "(fs/us) freq=%0.3fHz",
			 t, node->dt, freq_hz);
	}
#endif
}

void __chime_req_sim_speed_set(struct chime_request * req)
{
	float m = req->speed.val;
	struct ratio r;

	if (m < 0.000099) {
		WARN("Speed=%f times. Too slow!", m);
		return;
	}

	if (m > 1000000) {
		WARN("Speed=%f times. Too fast!", m);
		return;
	}

	float_to_ratio(&r, m, 10000);

	server.sim.period = (server.tmr.period * r.p) / r.q;

	INF("speed=%d/%d period=%"PRIu64" clk=%"PRIu64".", 
		r.p, r.q, server.sim.period, server.sim.clk);

	__sim_timer_reset();
}

void __chime_req_comm_create(struct chime_request * req)
{
	struct chime_comm * comm;

	INF("oid=%d", req->oid);

	comm = obj_getinstance_incref(req->oid);

	/* sanity check */
    assert(comm != NULL);

	/* insert OID in the comm's OID list */
	u16_list_insert(server.comm_oid, req->oid);

	/* alloc statistics distribution bins */
	comm->stat = malloc(COMM_STAT_BINS * sizeof(uint32_t));

	/* reset COMM */
	__chime_comm_reset(comm);
}

void __chime_req_comm_destroy(struct chime_request * req)
{
	struct chime_comm * comm;

	/* FIXME: destroy it for good */
	/* remove OID from comm's OID list */
	u16_list_remove(server.comm_oid, req->oid);

	comm = obj_getinstance(req->oid);

	/* release statistics distribution bins */
	free(comm->stat);
}

void __chime_req_init(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node = server.node[node_id];

	/* sanity check */
    if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
		assert(node != NULL);
		return;
	}


	DBG("<%d> session %d.", node_id, req->init.sid);

	if (req->init.sid != server.sim.sid) {
		WARN("invalid session ID %d.", req->init.sid);
		return;
	}

	/* clear the breakpoint flag */
//	node->bkpt = false;
	/* increment the checkout counter */
//	server.sim.checkout_cnt++;

	/* set the session ID */
	node->sid = server.sim.sid;
}

void __chime_req_reset_all(struct chime_request * req)
{
	uint8_t err[CHIME_NODE_MAX + 1];
	uint32_t sid;
	int i;

	INF("\"FIAT LUX!\"");
	/* And God said, Let there be light: and there was light. */

	DBG1("reseting the simulation...");
	__chime_sim_reset();

	DBG1("reseting the communication...");

	for (i = 1; i <= LIST_LEN(server.comm_oid); ++i) {
		struct chime_comm * comm;

		DBG3("Comm OID=%d...", server.comm_oid[i]);
		
		comm = obj_getinstance(server.comm_oid[i]);
		__chime_comm_reset(comm);
	}

	DBG1("reseting variable records...");
	for (i = 1; i <= LIST_LEN(server.var_oid); ++i) {
		struct chime_var * var;

		var = obj_getinstance(server.var_oid[i]);
		__chime_var_reset(var);
	}

	/* assign a new session id */
	sid = ++server.sim.sid;
	DBG1("session %d", sid);

	/* initialize list of errors */
	u8_list_init(err);

	DBG1("reseting nodes...");
	for (i = 1; i <= LIST_LEN(server.node_idx); ++i) {
		int node_id;

		node_id = server.node_idx[i];
		INF("<%d> reset...", node_id);

		if (!__chime_node_reset(node_id, sid)) {
			WARN("<%d> reset failed!.", node_id);
			/* insert on the error list */
			u8_list_insert(err, node_id);
		}
	}

	/* And God saw the light, and it was good; 
	   and God divided the light from the darkness */
	if (LIST_LEN(err) > 0) {
		DBG("removing dead nodes...");
		for (i = 1; i <= LIST_LEN(err); ++i) {
			__chime_node_remove(err[i]);
		}
	}

	DBG4("let the fun begin...");
}

void __chime_req_reset_cpu(struct chime_request * req)
{
	struct chime_node * node;

	node = obj_getinstance(req->oid);
	assert(node != NULL);

	DBG1("OID=%d.", req->oid);

	if (node->id == 0) {
		ERR("Invalid node ID.");
		return;
	}

	server.sim.checkout_cnt--;

	if (!__chime_node_reset(node->id, server.sim.sid)) {
		WARN("<%d> reset failed!.", node->id);
		__chime_node_remove(node->id);
	}

	/* reset simulation timer */
	__sim_timer_reset();

	if (server.sim.checkout_cnt == 0) {
		__chime_sim_step();
	}
}

void __chime_req_comm_stat(struct chime_request * req)
{
	int i;

	for (i = 1; i <= LIST_LEN(server.comm_oid); ++i) {
		struct chime_comm * comm;

		comm = obj_getinstance(server.comm_oid[i]);
		__chime_comm_stat_dump(comm);
	}
}

/* Create a variable object */
void __chime_req_var_create(struct chime_request * req)
{
	struct chime_var * var;

	INF("oid=%d", req->oid);

	var = obj_getinstance(req->oid);
	var->len = 2048;
	var->rec = malloc(var->len * sizeof(struct var_rec));
	assert(var->rec != NULL);

	/* insert OID in the var's OID list */
	u16_list_insert(server.var_oid, req->oid);

	/* reset the variable */
	__chime_var_reset(var);
}

void __chime_req_var_dump(struct chime_request * req)
{
	int i;

	for (i = 1; i <= LIST_LEN(server.var_oid); ++i) {
		struct chime_var * var;

		var = obj_getinstance(server.var_oid[i]);
		__chime_var_dump(var);
		__chime_var_flush(var);
	}
}

void __chime_req_var_rec(struct chime_request * req)
{
	int node_id = req->node_id;
	struct chime_node * node;
	struct chime_var * var;
	struct var_rec * rec;

	DBG3("<%d> oid=%d val=%f", node_id, req->oid, req->rec.val);

	node = server.node[node_id];

	/* sanity check */
    if (node == NULL) {
		WARN("<%d> invalid node!!!", node_id);
//		assert(node != NULL);
		return;
	}

	assert(node_id == node->id);

	var = obj_getinstance(req->oid);

	/* check for space availability. If we are short,
	   realloc() doubling the previous length. */
	if (var->cnt == var->len) {
		var->len *= 2;
		var->rec = realloc(var->rec, var->len * sizeof(struct var_rec));
	}

	/* Finally store the record value and increment the record count */
	rec = &var->rec[var->cnt++];
	rec->t = node->time;
	rec->y = req->rec.val;
}

static int chime_ctrl_task(void * arg)
{
	uint32_t buf[CHIME_REQUEST_LEN / 4];
	struct chime_request * req = (struct chime_request *)buf;
	__mq_t mq = server.mq;
	ssize_t len;

	__thread_init("CTRL");

	INF("simulation thread started.");

	while ((len = __mq_recv(mq, req, CHIME_REQUEST_LEN)) >= 0) {

		DBG3("<%d> [%s]", req->node_id, __req_opc_nm[req->opc]);

		switch (req->opc) {
		case CHIME_REQ_TMR0:
		case CHIME_REQ_TMR1:
		case CHIME_REQ_TMR2:
		case CHIME_REQ_TMR3:
		case CHIME_REQ_TMR4:
		case CHIME_REQ_TMR5:
		case CHIME_REQ_TMR6:
		case CHIME_REQ_TMR7:
			__chime_req_timer(req);
			break;

		case CHIME_REQ_XMT0:
		case CHIME_REQ_XMT1:
		case CHIME_REQ_XMT2:
		case CHIME_REQ_XMT3:
		case CHIME_REQ_XMT4:
		case CHIME_REQ_XMT5:
		case CHIME_REQ_XMT6:
		case CHIME_REQ_XMT7:
			__chime_req_comm_xmt(req);
			break;

		case CHIME_SIG_SIM_PAUSE:
			__chime_sig_pause_sim(req);
			break;

		case CHIME_SIG_SIM_RESUME:
			__chime_sig_resume_sim(req);
			break;

		case CHIME_SIG_SIM_TICK:
			__chime_sig_sim_tick(req);
			break;

		case CHIME_REQ_JOIN:
			__chime_node_join(req);
			break;

		case CHIME_REQ_CPU_HALT:
			__chime_req_cpu_halt(req);
			break;

		case CHIME_REQ_BYE:
			__chime_req_bye(req);
			break;

		case CHIME_REQ_INIT:
			__chime_req_init(req);
			break;

		case CHIME_REQ_ABORT:
			__chime_req_abort(req);
			break;

		case CHIME_REQ_STEP:
			__chime_req_step(req);
			break;

		case CHIME_REQ_BKPT :
			__chime_req_bkpt(req);
			break;

		case CHIME_REQ_SIM_TEMP_SET:
			__chime_req_temp_set(req);
			break;

		case CHIME_REQ_SIM_SPEED_SET:
			__chime_req_sim_speed_set(req);
			break;

		case CHIME_REQ_COMM_CREATE:
			__chime_req_comm_create(req);
			break;

		case CHIME_REQ_RESET_ALL:
			__chime_req_reset_all(req);
			break;

		case CHIME_REQ_TRACE:
			__chime_req_trace((struct chime_req_trace *)req);
			break;

		case CHIME_REQ_COMM_STAT:
			__chime_req_comm_stat(req);
			break;
			
		case CHIME_REQ_VAR_CREATE:
			__chime_req_var_create(req);
			break;
			
		case CHIME_REQ_VAR_REC:
			__chime_req_var_rec(req);
			break;

		case CHIME_REQ_VAR_DUMP:
			__chime_req_var_dump(req);
			break;

		case CHIME_REQ_CPU_RESET:
			__chime_req_reset_cpu(req);
			break;
		}
	}

	ERR("__mq_recv() failed: %s.", __strerr());

	return 0;
}

void chime_server_pause(void)
{
	struct chime_req_hdr req;

	/* pause the simulator */
	req.node_id = 0;
	req.opc = CHIME_SIG_SIM_PAUSE;
	req.oid = 0;
	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
	else
		while (!server.sim.paused)
			__msleep(100);
}

void chime_server_reset(void)
{
	struct chime_req_hdr req;

	/* pause the simulator */
	req.node_id = 0;
	req.opc = CHIME_REQ_RESET_ALL;
	req.oid = 0;
	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
}

void chime_server_resume(void)
{
	struct chime_req_hdr req;

	req.node_id = 0;
	req.opc = CHIME_SIG_SIM_RESUME;
	req.oid = 0;
	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
};

void chime_server_speed_set(float val)
{
	struct chime_req_float_set req;

	req.hdr.node_id = 0;
	req.hdr.opc = CHIME_REQ_SIM_SPEED_SET;
	req.hdr.oid = 0;
	req.val = val;

	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_FLOAT_SET_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
}

void chime_server_comm_stat(void)
{
	struct chime_req_hdr req;

	req.node_id = 0;
	req.opc = CHIME_REQ_COMM_STAT;
	req.oid = 0;
	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
}

void chime_server_var_dump(void)
{
	struct chime_req_hdr req;

	req.node_id = 0;
	req.opc = CHIME_REQ_VAR_DUMP;
	req.oid = 0;
	if (__mq_send(server.tmr.mq, &req, CHIME_REQ_HDR_LEN) < 0)
		ERR("__mq_send() failed: %s.", __strerr());
}

void chime_server_info(FILE * f)
{
	bool paused = server.sim.paused;

	if (!paused)
		chime_server_pause();

	fprintf(f, "---------------------------------------------------\n");

	fprintf(f, "objects alloc=%d free=%d\n", objpool_get_alloc(),
			objpool_get_free());

	fprintf(f, "tmr.tick_cnt=%d sim.tick_cnt=%d diff=%d\n",
		 server.tmr.tick_cnt, server.sim.tick_cnt,
		 server.tmr.tick_cnt - server.sim.tick_cnt);
	fprintf(f, "sim.clk=%"PRIu64"\n", server.sim.clk);
	heap_dump(f, server.heap);
	fprintf(f, "---------------------------------------------------\n");
	fflush(f);

	if (!paused)
		chime_server_resume();
};

int chime_server_start(const char * name)
{
	struct timeval tv;
	uint32_t period_ms;
	int ret = -1;
	int i;

	assert(OBJPOOL_OBJ_SIZE_MAX >= sizeof(struct chime_comm));
	assert(OBJPOOL_OBJ_SIZE_MAX >= sizeof(struct chime_node));
	assert(OBJPOOL_OBJ_SIZE_MAX >= sizeof(struct chime_var));

	__mutex_init(&server.mutex);
	__mutex_lock(server.mutex);

	if (!server.started) {

		gettimeofday(&tv, NULL);

		/* initialize node allocation */
		__chime_node_alloc_init();
		u8_list_init(server.node_idx);
		/* intialize vector of nodes */
		for (i = 1; i <= CHIME_NODE_MAX; ++i)
			server.node[i] = NULL;

		/* initialize comm OID list */
		u16_list_init(server.comm_oid);

		/* initialize variables OID list */
		u16_list_init(server.var_oid);

		/* Initial simulation speed (real time) */
		server.tmr.ack = 0;
		server.tmr.req = 0;
		server.tmr.period = 10 * MSEC;
		server.tmr.tick_cnt = 0;
		/* initialize simulation at 1x speed */
		server.sim.period = server.tmr.period;
		server.sim.clk = 0;
		server.sim.tick_cnt = 0;
		server.sim.tick_lost = 0;
		server.sim.paused = false;
		server.sim.checkout_cnt = 0;
		/* set initial session id.
		  The session id is incremented on each reset.
		  It's used to synchronize nodes. 
		  Nodes with wrong session ids are ignored 
		  from the current simulation session.*/
		server.sim.sid = tv.tv_sec; 
		/* set initial temperature */
		server.temperature = 25.0;

		/* initial probe sequence */
		server.probe_seq = 1000000 + tv.tv_usec;

		/* make sure we got rid of an existing message queue file */
		__mq_unlink(name);
		/* save the path name */
		strncpy(server.mqname, name, PATH_MAX - 1);
		server.mqname[PATH_MAX - 1] = '\0';

		INF("server='%s' sid=%d.", name, server.sim.sid);

		do {

			/* allocate the pool of objects */
			INF("creating object pool...");
			if (objpool_create(name, 8192) < 0) {
				ERR("objpool_create() failed.");
				break;
			}

			INF("allocating server shared structure...");
			server.shared = obj_alloc();
			server.shared_oid = obj_oid(server.shared);
			assert(server.shared_oid == SRV_SHARED_OID);
			/* initialize shared structure */
			__dir_lst_clear(&server.shared->dir);
			server.shared->magic = SRV_SHARED_MAGIC;
			server.shared->time = 0;

			/*
			 *  Allocating a clock heap
			 */
			INF("allocating clock heap...");
			if ((server.heap = clk_heap_alloc(1024 * CHIME_NODE_MAX)) == NULL) {
				ERR("clk_heap_alloc() failed.");
				break;
			}
			server.heap->clk = 0LL;

			INF("initializing trace buffer ...");
			if (__chime_trace_init() < 0) {
				ERR("__chime_trace_init() failed.");
				break;
			}

			INF("creating a message queue ...");
			if (__mq_create(&server.mq, name, CHIME_REQUEST_LEN) < 0) {
				ERR("__mq_create(\"%s\") failed: %s.", name, __strerr());
				break;
			}

			/*
			 *  Initialize simulation timer
			 */
			INF("initializing interval timer ...");
			if (__mq_open(&server.tmr.mq, name) < 0) {
				ERR("__mq_open(\"%s\") failed: %s.", name, __strerr());
				break;;
			}
			period_ms = TS2MSEC(server.tmr.period);
			INF("period=%d ms.", period_ms);
			if (__itmr_init(period_ms, __sim_timer_isr) < 0) {
				ERR("__itmr_init() failed!");
				break;
			}

			INF("creating thread ...");
			server.enabled = true;
			if ((ret = __thread_create(&server.ctrl_thread, 
									  (void * (*)(void *))chime_ctrl_task,
									  (void *)NULL)) < 0) {
				ERR("__thread_create() failed: %s.", strerror(ret));
				server.enabled = false;
				unlink(server.mqname);
				break;
			}

			server.started = true;

			INF("server initialized.");
			ret = 0;
		} while (0);
	} else {
		ERR("server already running.");
	}

	__mutex_unlock(server.mutex);

	return ret;
}

int chime_server_stop(void)
{
	int ret;

	__mutex_lock(server.mutex);

	if (server.started) {
		server.enabled = false;

		__itmr_stop();
		__mq_close(server.tmr.mq);
		__mq_close(server.mq);

		__thread_cancel(server.ctrl_thread);
		__thread_join(server.ctrl_thread, NULL);

		__mq_unlink(server.mqname);

		objpool_close();
		objpool_destroy();

		server.started = false;
		ret = 0;
	} else {
		ret = -1;
		ERR("server not running.");
	}

	__mutex_unlock(server.mutex);

	return ret;
}


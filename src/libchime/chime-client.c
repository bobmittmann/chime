#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include <fcntl.h>

#define __CHIME_I__
#include "chime-i.h"
#include "chime-cpu.h"

#include "objpool.h"
#include "list.h"

struct chime_client {
	bool started;
	volatile bool enabled;
	__mutex_t mutex;
	uint32_t cpu_cnt;
	__mq_t mqsrv;
	__sem_t except_sem;
	char name[64];
	struct {
		char name[64];
	} master;
	struct srv_shared * srv_shared;
	void * node_lst[CHIME_NODE_MAX + 1];
};

static struct chime_client client = {
	.started = false,
	.enabled = false,
	.cpu_cnt = 0
};

int chime_cpu_create(float offs_ppm, float tc_ppm, void (* on_reset)(void))
{
	struct chime_req_join req;
	int ret = -1;

	__mutex_lock(client.mutex);

	if (client.started) {
		do {
			struct chime_node * node;

			if ((node = obj_alloc()) == NULL) {
				ERR("obj_alloc() failed!");
				break;
			}

			snprintf(node->name, 63, "%s.%d", client.name, client.cpu_cnt);
			node->offs_ppm = offs_ppm;
			node->tc_ppm = tc_ppm;

			node->c.xmt_mq = client.mqsrv;
			node->c.client = &client;
			node->c.on_reset = on_reset;
			node->c.srv_shared = client.srv_shared;
			node->c.except = 0;
			node->c.except_sem = client.except_sem;
			node->sid = 0; /* set an invalid initial session id */

			DBG2("node OID=%d", obj_oid(node));
			DBG1("cpu=%s offs=%.3fppm tc=%.3fppm",
				node->name, node->offs_ppm, node->tc_ppm);

			/* remove existing queue */
			__mq_unlink(node->name);

			if (__mq_create(&node->c.rcv_mq, node->name, CHIME_EVENT_LEN) < 0) {
				ERR("__mq_create(\"%s\") failed: %s.", node->name, __strerr());
				obj_free(node);
				break;
			}

			DBG1("creating CPU thread ...");
			if ((ret = __thread_create(&node->c.thread,
									  (void * (*)(void *))__cpu_ctrl_task,
									  (void *)node)) < 0) {
				ERR("__thread_create() failed.");
				__mq_close(node->c.rcv_mq);
				__mq_unlink(node->name);
				obj_free(node);
				break;
			}

			/* try to connect to the server */
			req.hdr.node_id = 0;
			req.hdr.opc = CHIME_REQ_JOIN;
			req.hdr.oid = obj_oid(node);

			if (__mq_send(client.mqsrv, &req, CHIME_REQ_JOIN_LEN) < 0) {
				ERR("__mq_send() failed: %s.", __strerr());
				__mq_close(node->c.rcv_mq);
				__mq_unlink(node->name);
				__thread_cancel(node->c.thread);
				__thread_join(node->c.thread, NULL);
				obj_free(node);
				break;
			}

			client.cpu_cnt++;
			ptr_list_insert(client.node_lst, node);
			/* Return the CPU node id */
			ret = obj_oid(node);
		} while (0);

	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_cpu_reset(int cpu_oid)
{
	assert(obj_getinstance(cpu_oid) != NULL);

	return __cpu_req_send(CHIME_REQ_CPU_RESET, cpu_oid);
}

/* Run a CPU in the main thread */
int chime_cpu_run(float offs_ppm, float tc_ppm, void (* on_reset)(void))
{
	struct chime_node * node;
	struct chime_req_join req;

	__mutex_lock(client.mutex);

	if (!client.started) {
		ERR("client is not running!");
		__mutex_unlock(client.mutex);
		return -1;
	}


	if ((node = obj_alloc()) == NULL) {
		ERR("obj_alloc() failed!");
		__mutex_unlock(client.mutex);
		return -1;
	}

	snprintf(node->name, 63, "%s.%d", client.name, client.cpu_cnt);
	node->offs_ppm = offs_ppm;
	node->tc_ppm = tc_ppm;
	node->c.xmt_mq = client.mqsrv;
	node->c.client = &client;
	node->c.on_reset = on_reset;
	node->c.srv_shared = client.srv_shared;
	node->c.thread = __thread_self();
	node->sid = 0; /* set an invalid initial session id */

	DBG2("node OID=%d", obj_oid(node));
	DBG1("cpu=%s offs=%.3fppm tc=%.3fppm",
		node->name, node->offs_ppm, node->tc_ppm);

	/* remove existing queue */
	__mq_unlink(node->name);

	if (__mq_create(&node->c.rcv_mq, node->name, CHIME_EVENT_LEN) < 0) {
		ERR("__mq_create(\"%s\") failed: %s.", node->name, __strerr());
		obj_free(node);
		__mutex_unlock(client.mutex);
		return -1;
	}

	/* try to connect to the server */
	req.hdr.node_id = 0;
	req.hdr.opc = CHIME_REQ_JOIN;
	req.hdr.oid = obj_oid(node);

	if (__mq_send(client.mqsrv, &req, CHIME_REQ_JOIN_LEN) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		__mq_close(node->c.rcv_mq);
		__mq_unlink(node->name);
		obj_free(node);
		__mutex_unlock(client.mutex);
		return -1;
	}

	client.cpu_cnt++;
	ptr_list_insert(client.node_lst, node);
	__mutex_unlock(client.mutex);

	return __cpu_sim_loop(node);
}

static void __client_cpu_thread_init(void)
{
	/* initialize local thread storage variables */
	cpu.client = &client;
	cpu.srv_shared = client.srv_shared;
	cpu.rcv_mq = 0;
	cpu.xmt_mq = client.mqsrv;
	cpu.rst_isr = NULL;
	cpu.enabled = false; /* this is not a registered CPU yet */
	cpu.node_id = 0; /* Invalid node CPU */
	cpu.node = NULL;
}

int chime_client_start(const char * name)
{
	int ret = -1;

	__mutex_init(&client.mutex);
	__mutex_lock(client.mutex);

	if (!client.started) {

		do {

			/* open the server message queue */
			DBG1("opening server's message queue ...");
			if (__mq_open(&client.mqsrv, name) < 0) {
				ERR("__mq_open(\"%s\") failed: %s.", name, __strerr());
				break;
			}

			/* open the pool of objects */
			DBG1("opening object pool...");
			if (objpool_open(name) < 0) {
				ERR("objpool_open(\"%s\") failed: %s.", name, __strerr());
				break;
			}

			DBG1("server shared object get...");
			client.srv_shared = obj_getinstance_incref(SRV_SHARED_OID);
			if (client.srv_shared == NULL) {
				ERR("obj_getinstance_incref(%d) failed!", SRV_SHARED_OID);
				break;
			}

			DBG("server magic is %08X", client.srv_shared->magic);
			if (client.srv_shared->magic != SRV_SHARED_MAGIC) {
				ERR("invalid server magic number!");
				break;
			}

			DBG("CPU notification semaphore initialization");
			if (__sem_init(&client.except_sem, 0, 0) < 0) {
				ERR("__sem_init() failed: %s.", __strerr());
				break;
			}

			strcpy(client.master.name, name);
			sprintf(client.name, "%s.%d", name, getpid());
			client.enabled = true;
			client.started = true;
			/* initialize the list of nodes */
			ptr_list_init(client.node_lst);
			/* initialize the main thread local storage */
			__client_cpu_thread_init();
			ret = 0;
		} while (0);

	} else {
		ERR("client already running.");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_reset_all(void)
{
	bool ret = false;

	__mutex_lock(client.mutex);

	if (client.started) {
		ret = __cpu_req_send(CHIME_REQ_RESET_ALL, 0);
	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_sim_speed_set(float val)
{
	bool ret = false;

	__mutex_lock(client.mutex);

	if (client.started) {
		ret = __cpu_req_float_set(CHIME_REQ_SIM_SPEED_SET, 0, val);
	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_sim_pause(void)
{
	bool ret = false;

	__mutex_lock(client.mutex);

	if (client.started) {
		ret = __cpu_req_send(CHIME_SIG_SIM_PAUSE, 0);
	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_sim_resume(void)
{
	bool ret = false;

	__mutex_lock(client.mutex);

	if (client.started) {
		ret = __cpu_req_send(CHIME_SIG_SIM_RESUME, 0);
	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_sim_vars_dump(void)
{
	bool ret = false;

	__mutex_lock(client.mutex);

	if (client.started) {
		ret = __cpu_req_send(CHIME_REQ_VAR_DUMP, 0);
	} else {
		ERR("client is not running!");
	}

	__mutex_unlock(client.mutex);

	return ret;
}

bool chime_except_catch(struct chime_except * e)
{
	struct chime_node * node;
	int ret = false;
	int i;

	if (!client.started) {
		ERR("client not running.");
		return ret;
	}

	if (PTR_LIST_LEN(client.node_lst) == 0)
		return ret;

	__sem_wait(client.except_sem);

	__mutex_lock(client.mutex);

	for (i = 1; i <= PTR_LIST_LEN(client.node_lst); ++i) {
		node = client.node_lst[i];

		if (node->c.except != 0) {

			ptr_list_remove(client.node_lst, node);

			if (e != NULL) {
				e->node_id = node->id;
				e->code = node->c.except;;
				e->oid = obj_oid(node);
				e->cycles = node->ticks;
			}
			ret = true;

			break;
		}
	}

	__mutex_unlock(client.mutex);

	return ret;
}

static void __cpu_stop(struct chime_node * node)
{
	struct chime_req_hdr req;
	int node_id = node->id;

	/* FIXME this will not work on win32 */
	__thread_t self = __thread_self();

	if (node->c.except == 0) {
#ifdef _WIN32
		if (memcmp(&node->c.thread, &self, sizeof(pthread_t)) == 0) {
#else
		if (node->c.thread != self) {
#endif
			DBG1("<%d> thread cancel...", node_id);
			__thread_cancel(node->c.thread);
			DBG1("<%d> thread join ...", node_id);
			__thread_join(node->c.thread, NULL);
		}

		req.node_id = node_id;
		req.opc = CHIME_REQ_BYE;
		req.oid = obj_oid(node);

		DBG1("<%d> oid=%d says good-bye...", node_id, req.oid);

		if (__mq_send(client.mqsrv, &req, CHIME_REQ_HDR_LEN) < 0) {
			ERR("__mq_send() failed: %s.", __strerr());
		}
	}

	DBG5("<%d> __mq_close()", node_id);

	/* close CPU's message queue */
	__mq_close(node->c.rcv_mq);

	DBG5("<%d> __mq_unlink()", node_id);

	/* remove message queue name */
	__mq_unlink(node->name);

	DBG5("<%d> obj_decref()", node_id);

	/* decrement object reference count */
	obj_decref(node);

	DBG5("<%d> done!", node_id);
}

int chime_client_stop(void)
{
	struct chime_node * node;
	int ret;
	int i;

	DBG("...");

	__mutex_lock(client.mutex);

	if (client.started) {
		client.enabled = false;

		/* stop CPUs */
		for (i = 1; i <= PTR_LIST_LEN(client.node_lst); ++i) {
			node = client.node_lst[i];
			DBG1("<%d> closing...", node->id);
			__cpu_stop(node);
		}

		/* close connection with server */
		__mq_close(client.mqsrv);

		/* release server shared object */
		obj_decref(client.srv_shared);
		/* close pool of objects */
		objpool_close();
		client.started = false;
		ret = 0;
	} else {
		ret = -1;
		ERR("client not running.");
	}

	__mutex_unlock(client.mutex);

	return ret;
}


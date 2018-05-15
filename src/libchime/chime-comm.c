#include <errno.h>
#include <string.h>

#define __CHIME_CPU__
#include "chime-cpu.h"

#include "list.h"

/****************************************************************************
  Client scope 
 ****************************************************************************/

int chime_comm_create(const char * name, struct comm_attr * attr)
{
	struct srv_shared * shared = cpu.srv_shared;
	struct chime_comm * comm;
	int oid;

	DBG1("name=%s", name);

	objpool_lock();
	oid = __dir_lst_lookup(&shared->dir, name);
	objpool_unlock();

	/* sanity check */
	assert(attr->bytes_max > 0);
	assert(attr->bytes_max <= OBJPOOL_OBJ_SIZE_MAX);
	assert(attr->speed_bps > 0);

	if (oid != OID_NULL) {
		WARN("COMM \"%s\" already exists!", name);
		comm = obj_getinstance(oid);
		objpool_lock();
		comm->attr = *attr; /* update attributes */
		objpool_unlock();
		return oid;
	} 

	if ((comm = obj_alloc()) == NULL) {
		ERR("object allocation failed!");
		return -1;
	}		

	oid = obj_oid(comm);
	DBG1("comm=%p oid=%d", comm, oid);

	/* initialize object */
	objpool_lock();
	comm->attr = *attr;
	strncpy(comm->name, name, ENTRY_NAME_MAX);
	u8_list_init(comm->node_lst);
	objpool_unlock();

	if (__cpu_req_send(CHIME_REQ_COMM_CREATE, oid)) {
		/* insert into directory */
		objpool_lock();
		__dir_lst_insert(&shared->dir, name, oid);
		objpool_unlock();
	} else {
		/* release the object */
		obj_free(comm);
		return -1;
	}

	return oid;
}

int chime_comm_destroy(const char * name)
{
	ERR("NOT implemented!");
	return -1;
}

/****************************************************************************
  CPU scope 
 ****************************************************************************/

int chime_comm_attach(int chan, const char * name, 
					  void (* rcv_isr)(void), 
					  void (* eot_isr)(void), 
					  void (* dcd_isr)(void))
{
	struct chime_comm * comm;
	int oid;

	assert((unsigned int)chan < CHIME_CPU_COMM_MAX);  

	DBG1("name=%s", name);
	DBG3("cpu.srv_shared=%p", cpu.srv_shared);

	objpool_lock();
	oid = __dir_lst_lookup(&cpu.srv_shared->dir, name);
	objpool_unlock();

	if (oid == OID_NULL) {
		ERR("COMM \"%s\" don't exist!", name);
		__cpu_except(EXCEPT_COMM_LOOKUP_FAIL);
		return -1;
	}

	DBG2("oid=%d", oid);

	comm = obj_getinstance_incref(oid);

	DBG1("comm=%p oid=%d", comm, oid);

	assert(comm != NULL);

	cpu.comm[chan].oid = oid;
	cpu.comm[chan].rx_buf = NULL;
	cpu.comm[chan].rx_len = 0;
	cpu.comm[chan].rcv_isr = rcv_isr;
	cpu.comm[chan].eot_isr = eot_isr;
	cpu.comm[chan].dcd_isr = dcd_isr;
	cpu.comm[chan].tx_busy = false;

	objpool_lock();
	if (!u8_list_contains(comm->node_lst, cpu.node_id))
		u8_list_insert(comm->node_lst, cpu.node_id);
	else
		DBG("CPU is already in the list");
	objpool_unlock();

	return 0;
}

int chime_comm_close(int chan)
{
	struct chime_comm * comm;

	assert((unsigned int)chan < CHIME_CPU_COMM_MAX);  

	comm = obj_getinstance(cpu.comm[chan].oid);

	assert(comm != NULL);

	objpool_lock();
	u8_list_remove(comm->node_lst, cpu.node_id);
	objpool_unlock();

	obj_decref(comm);

	return 0;
}

int chime_comm_write(int chan, const void * buf, size_t len)
{
	struct chime_req_comm req;
	int comm_oid;
	void * frm;

	assert((unsigned int)chan < CHIME_CPU_COMM_MAX);  

	comm_oid = cpu.comm[chan].oid;
	assert(comm_oid != 0);

	if (cpu.comm[chan].tx_busy) {
		ERR("COMM TX busy!");
		__cpu_except(EXCEPT_COMM_TX_BUSY);
	}

	/* allocate an frame */
	DBG("Allocating COMM frame!");
	if ((frm = obj_alloc()) == NULL) {
		ERR("object allocation failed!");
		__cpu_except(EXCEPT_OBJ_ALLOC_FAIL);
	}		

	len = MIN(len, OBJPOOL_OBJ_SIZE_MAX);
	memcpy(frm, buf, len);

	req.hdr.oid = comm_oid;
	req.hdr.node_id = cpu.node_id;
	req.hdr.opc = CHIME_REQ_XMT0 + chan;
	req.buf_oid = obj_oid(frm);
	req.buf_len = len;

	DBG1("<%d> COMM{chan=%d oid=%d} buf{oid=%d len=%d}.", 
		 cpu.node_id, chan, comm_oid, req.buf_oid, (int)len);

	if (__mq_send(cpu.xmt_mq, &req, CHIME_REQ_COMM_LEN) < 0) {
		ERR("__mq_send() failed: %s.", __strerr());
		obj_free(frm);
		__cpu_except(EXCEPT_MQ_SEND);
	}

	cpu.comm[chan].tx_busy = true;

	return len;
}


int chime_comm_read(int chan, void * buf, size_t len)
{
	assert((unsigned int)chan < CHIME_CPU_COMM_MAX);  

	if (cpu.comm[chan].rx_len == 0)
		return -1;

	if (len > 0) {
		len = MIN(cpu.comm[chan].rx_len, len);
		memcpy(buf, cpu.comm[chan].rx_buf, len);
	}

	DBG1("<%d> COMM chan=%d len=%d.", cpu.node_id, chan, (int)len);

	cpu.comm[chan].rx_len = 0;
	obj_decref(cpu.comm[chan].rx_buf);

	return len;
}

int chime_comm_nodes(int chan)
{
	struct chime_comm * comm;
	int comm_oid;

	assert((unsigned int)chan < CHIME_CPU_COMM_MAX);  

	comm_oid = cpu.comm[chan].oid;
	assert(comm_oid != 0);

	comm = obj_getinstance(comm_oid);
	assert(comm != NULL);

	return LIST_LEN(comm->node_lst);
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "chime.h"
#include "debug.h"
#include "fx-arcnet.h"

#include "fx-net.h"

/****************************************************************************
 * ARCnet Driver
 ****************************************************************************/

#define ARCNET_HEAP_LEN 1024

__thread struct {
	struct {
        bool idle;
		struct heap_entry last;
		struct pkt_heap heap;
		struct heap_entry e[ARCNET_HEAP_LEN + 1];
	} tx;
	struct {
		uint32_t tx_count;
		uint32_t tx_retry;
		uint32_t tx_fail;
		uint32_t tx_overflow;
	} stat;
} arcnet_drv;


static inline void arcnet_pkt_free(struct arcnet_pkt * pkt)
{
	fx_pkt_free(pkt);
}

static inline struct arcnet_pkt * arcnet_pkt_alloc(void)
{
	struct arcnet_pkt * pkt;

	pkt = fx_pkt_alloc();

	return pkt;
}

void arcnet_rcv_isr(void)
{
	struct arcnet_pkt * pkt;

	pkt = arcnet_pkt_alloc();

	arcnet_pkt_read(pkt);

	INF("<%d> src=%d typ=%d <<<<<<<<<<< ",
		chime_cpu_id(), pkt->hdr.src, pkt->hdr.typ);

	tracef(T_INF, "<%d> ARCnet RECV %d->%d (%d, %d) '%s'", chime_cpu_id(),
		   pkt->hdr.src, pkt->hdr.dst, pkt->hdr.typ, pkt->hdr.len,
		   (char *)pkt->data);

	arcnet_pkt_free(pkt);
}

void arcnet_eot_isr(void)
{
	struct arcnet_pkt * pkt;
	struct heap_entry e;

	if (arcnet_drv.tx.last.pkt != NULL) {
		if (arcnet_tx_stat() == ARCNET_TX_FAIL) {
			if ((arcnet_drv.tx.last.ord & RETRY_MASK) != 0) {
				/* increment the entry order (retry counter) */
				arcnet_drv.tx.last.ord++;
				/* update statistics */
				arcnet_drv.stat.tx_retry++;
				/* put back into the queue */
				fx_pkt_heap_insert(&arcnet_drv.tx.heap, arcnet_drv.tx.last);
			} else {
				arcnet_drv.stat.tx_fail++;
				arcnet_pkt_free(arcnet_drv.tx.last.pkt);
			}
		} else {
			arcnet_drv.stat.tx_count++;
			arcnet_pkt_free(arcnet_drv.tx.last.pkt);
		}
	}

	/* get a packet from priority queue */
	if (fx_pkt_heap_extract_min(&arcnet_drv.tx.heap, &e)) {
		pkt = e.pkt;
		/* update the destination address */
		pkt->hdr.dst = e.dst;
		/* write to the TX buffer */
		arcnet_pkt_write(pkt);

		INF("<%d> src=%d dst=%d.", chime_cpu_id(),
			pkt->hdr.src, pkt->hdr.dst);

		arcnet_drv.tx.idle = false;
		/* keep track of the last packet sent */
		arcnet_drv.tx.last = e;
	} else {
	    arcnet_drv.tx.idle = true;
   		/* cler the last packet sent buffer */
		arcnet_drv.tx.last.pkt = NULL;
	}
//	INF("<%d> src=%d sc=%3d...", chime_cpu_id(), src, sc);
}


/* Initialize Arcnet driver */
void arcnet_drv_init(int addr)
{
	arcnet_mac_open(addr, arcnet_rcv_isr, arcnet_eot_isr);
	arcnet_drv.tx.idle = true;
	fx_pkt_heap_init(&arcnet_drv.tx.heap, ARCNET_HEAP_LEN);
}

/* Enqueue a packet into the transmission queue */
bool arcnet_drv_enqueue(uint8_t dst, uint8_t pri,
						uint8_t retry, struct arcnet_pkt * pkt)
{
	struct heap_entry e;
	bool ret;

	/* Prepare a heap entry */
	e.ord = fx_pkt_heap_mk_ord(&arcnet_drv.tx.heap, pri, retry);
	e.dst = dst;
	e.pkt = pkt;

	/* Insert into priority queue */
	if (!(ret = fx_pkt_heap_insert(&arcnet_drv.tx.heap, e))) {
		ERR("fx_pkt_heap_insert() failed!");
		/* Insertion failed. Update statistics and release the packet */
		fx_pkt_decref(pkt);
		arcnet_drv.stat.tx_overflow++;
	} else {
	    if (arcnet_drv.tx.idle) {
			DBG5("First packet XMIT now!");
			/* start tranmission */
//			arcnet_eot_isr();
		}
	}

	return ret;
}

void arcnet_drv_tx_enable(void)
{
    if (arcnet_drv.tx.idle) {
		/* start tranmission */
		arcnet_eot_isr();
	}
}

/* Enqueue multiple copies into the transmission queue.
   dst[] is an array of destination nodes, the last
   element of the list mus be 0. */
void arcnet_drv_mcast_enqueue(uint8_t dst[], uint8_t pri,
							  uint8_t retry, struct arcnet_pkt * pkt)
{
	int i;

	for (i = 0; dst[i] != 0; ++i) {
		struct heap_entry e;

		/* Prepare a heap entry */
		e.ord = fx_pkt_heap_mk_ord(&arcnet_drv.tx.heap, pri, retry);
		e.dst = dst[i];
		e.pkt = pkt;

		/* Insert into priority queue */
		if (fx_pkt_heap_insert(&arcnet_drv.tx.heap, e)) {
			/* Increment packet reference count */
			fx_pkt_incref(pkt);
		} else {
			/* Insertion failed. Update statistics */
			ERR("fx_pkt_heap_insert() failed!");
			arcnet_drv.stat.tx_overflow++;
		}
	}

	/* Decrement packet reference count.
	   When the packet was allocated the reference count was set to 1. For
	   each insertion into the queue the reference count was increased by one.
	   We need to decrement the reference once, to discount the initial
	   allocation, keeping the packet usage sane. */
	fx_pkt_decref(pkt);
}

/* allocate a packet buffer, and enqueue */
bool arcnet_drv_send(int dst, int pri, int proto, void * pdu, int len)
{
	struct arcnet_pkt * pkt;

	if ((pkt = arcnet_pkt_alloc()) == NULL) {
		ERR("arcnet_pkt_alloc() failed!");
		return false;
	}

	pkt->hdr.typ = proto;
	pkt->hdr.len = len;
	pkt->hdr.src = arcnet_mac_addr();
	pkt->hdr.dst = dst;
	memcpy(pkt->data, pdu, len);

	DBG5("<%d> %d->%d (%d, %d) %d pkt=%p", 
		chime_cpu_id(), pkt->hdr.src, pkt->hdr.dst, 
		pkt->hdr.typ, pkt->hdr.len, pri, pkt);

	/* write to the TX buffer */
//	arcnet_pkt_write(pkt);

//	tracef(T_INF, "<%d> ARCnet XMIT %d->%d (%d, %d)", chime_cpu_id(),
//		   pkt->hdr.src, pkt->hdr.dst, pkt->hdr.typ, pkt->hdr.len);

	return arcnet_drv_enqueue(dst, pri, 2, pkt);
}


/****************************************************************************
 * ...
 ****************************************************************************/

#define MSG_MAGIC 0xdefecada

struct msg {
	uint32_t seq;
	uint32_t magic;
	uint32_t pri;
};

void timer0_isr(void)
{
	static __thread struct msg msg = {
		.seq = 0,
		.magic = MSG_MAGIC
	};

	INF("<%d> clk=%d .........................",
		chime_cpu_id(), chime_cpu_cycles());

	tracef(T_INF, "<%d> burst %d", chime_cpu_id(), msg.seq);

	msg.pri = 3;
	arcnet_drv_send(0, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(2, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(3, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(4, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;

	msg.pri = 2;
	arcnet_drv_send(0, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(2, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(3, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(4, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;

	msg.pri = 0;
	arcnet_drv_send(0, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(2, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(3, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(4, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;

	msg.pri = 1;
	arcnet_drv_send(0, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(2, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(3, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
	arcnet_drv_send(4, msg.pri, 0, &msg, sizeof(msg));
	msg.seq++;
}

void arcnet_drv_tx_flush(FILE* f)
{
	struct pkt_heap * heap = &arcnet_drv.tx.heap;
	uint16_t seq = heap->seq;
	struct heap_entry e;
	int16_t key;
	int16_t prev_key;
	struct arcnet_pkt * pkt;
	struct msg * msg;

	fprintf(f, "\n %6d --------------------\n", seq);

//	fx_pkt_heap_dump(stdout, heap);

	if (!fx_pkt_heap_extract_min(heap, &e)) {
		fprintf(f, " - heap empty!\n");
		return;
	}

	prev_key = (int16_t)(e.ord - seq);

	do {
		key = (int16_t)(e.ord - seq);

		pkt = e.pkt;
		msg = (struct msg *)pkt->data;

		if (key < prev_key) {
			fprintf(f, " %6d <-- ERROR\n", key);
		} else {
			fprintf(f, " %6d %d->%d (%d, %d) %d %4d %p\n", 
					key, pkt->hdr.src, pkt->hdr.dst, 
					pkt->hdr.typ, pkt->hdr.len, msg->pri, msg->seq,
					pkt);
		}

		prev_key = key;

		arcnet_pkt_free(pkt);
//		fx_pkt_heap_dump(stdout, heap);

	} while (fx_pkt_heap_extract_min(heap, &e));

	fflush(f);
}


void timer1_isr(void)
{
	tracef(T_INF, "<%d> timer 1 timeout", chime_cpu_id());

//	fx_pkt_heap_dump(stdout, &arcnet_drv.tx.heap);
	arcnet_drv_tx_flush(stdout);
//	exit(1);
}

void cpu0_reset(void)
{
	tracef(T_INF, "<%d> Initializing ARCnet", chime_cpu_id());
	arcnet_drv_init(0);

	/* Initialize a timer */
	chime_tmr_init(0, timer0_isr, 500000,  50000);
	chime_tmr_init(1, timer1_isr, 500100 + 64 * 50000, 64 * 50000);

	for (;;) {
		chime_cpu_wait();
	}
}

void cpu1_reset(void)
{
	tracef(T_INF, "<%d> Initializing ARCnet", chime_cpu_id());

	arcnet_drv_init(0);

	for (;;) {
		chime_cpu_wait();
	}
}

#define NODE_COUNT 4

int main(int argc, char *argv[])
{
	int cpu[NODE_COUNT];
	int i;

	printf("\n==== ARCnet simulation! ====\n");
	fflush(stdout);

	if (chime_client_start("chronos") < 0) {
		fprintf(stderr, "chime_client_start() failed!\n");
		fflush(stderr);
		return 1;
	}

	chime_app_init((void (*)(void))chime_client_stop);

	fx_pkt_pool_init();
	fx_arcnet_sim_init();

	cpu[0] = chime_cpu_create(0, 0, cpu0_reset);

	for (i = 1; i < NODE_COUNT; ++i) {
		cpu[i] = chime_cpu_create(0, 0, cpu1_reset);
	}

//	cpu1 = chime_cpu_create(0, 0, cpu1_reset);
//	(void)cpu1;

//	chime_reset_all();

	for (i = 0; i < NODE_COUNT; ++i) {
		chime_cpu_reset(cpu[i]);
	}

	while (chime_except_catch(NULL));

	chime_client_stop();

	return 0;
}


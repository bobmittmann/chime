/* 
 * File:	 objpool.c
 * Author:   Robinson Mittmann (bobmittmann@gmail.com)
 * Target:
 * Comment:
 * Copyright(C) 2013 Bob Mittmann. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* 
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "fx-spinlock.h"

#ifndef PKT_SIZE_MAX 
#define PKT_SIZE_MAX 256
#endif /* PKT_SIZE_MAX */

#ifndef PKT_POOL_LEN 
#define PKT_POOL_LEN 1024
#endif /* PKT_POOL_LEN */

#define __OID_VOID -1

/* object container */
struct obj {
	struct {
		uint16_t ref; /* reference counter */
		uint16_t oid; /* object id */
	} meta;
#define SIZEOF_META (2 * sizeof(uint16_t))
#define META_OFFS (SIZEOF_META / sizeof(uint32_t))
	union {
		uint16_t next;
		uint32_t data[PKT_SIZE_MAX / 4];
    };
} __attribute__((aligned(4)));

struct {
	uint32_t error; /* allocation fail errors */
	int32_t head;
	int32_t tail;
	__spinlock_t spinlock;
	struct obj obj[PKT_POOL_LEN];
} pkt_pool;

void * fx_pkt_alloc(void)
{
	void * ret = NULL;
	int oid;

	__spin_lock(pkt_pool.spinlock);

	if ((oid = pkt_pool.head) != __OID_VOID) {
		struct obj * obj = &pkt_pool.obj[oid];

		/* sanity check */
		assert(obj->meta.oid == oid);
		assert(obj->meta.ref == 0);

		if (pkt_pool.head == pkt_pool.tail) 
			pkt_pool.head = pkt_pool.tail = __OID_VOID; /* Pool is empty */
		else
			pkt_pool.head = obj->next;
		
		obj->meta.ref = 1; /* initialize reference counter */

		ret = (void *)obj->data;
	} else {
		pkt_pool.error++;
	}

	__spin_unlock(pkt_pool.spinlock);

	return ret;
}

int fx_pkt_incref(void * ptr)
{
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int ret;

	assert(ptr != NULL);

	__spin_lock(pkt_pool.spinlock);

	assert(obj->meta.ref > 0);

	ret = obj->meta.ref++;

	__spin_unlock(pkt_pool.spinlock);

	return ret;
}

int fx_pkt_decref(void * ptr)
{
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int oid;
	int ret;

	assert(ptr != NULL);

	__spin_lock(pkt_pool.spinlock);

	oid = obj->meta.oid;

	assert(obj == &pkt_pool.obj[oid]);
	assert(obj->meta.ref > 0);

	if ((ret = --obj->meta.ref) == 0) { 
		if (pkt_pool.head == __OID_VOID) 
			pkt_pool.head = oid;
		else {
			pkt_pool.obj[pkt_pool.tail].next = oid;
		}
		pkt_pool.tail = oid;
	}

	__spin_unlock(pkt_pool.spinlock);

	return ret;
}

void fx_pkt_free(void * ptr)
{
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int oid;

	assert(ptr != NULL);

	__spin_lock(pkt_pool.spinlock);

	oid = obj->meta.oid;

	assert(obj->meta.ref == 1);

	obj->meta.ref = 0;
	if (pkt_pool.head == __OID_VOID) 
		pkt_pool.head = oid;
	else {
		pkt_pool.obj[pkt_pool.tail].next = oid;
	}
	pkt_pool.tail = oid;

	__spin_unlock(pkt_pool.spinlock);
}

void fx_pkt_pool_init(void)
{
	size_t nmemb = PKT_POOL_LEN;
	struct obj * obj;
	int oid;

	__spinlock_create(&pkt_pool.spinlock);

	for (oid = 0; oid < nmemb; ++oid) {
		obj = &pkt_pool.obj[oid];
		obj->meta.oid = oid;
		obj->meta.ref = 0;
		obj->next = oid + 1;
	}

	pkt_pool.head = 0;
	pkt_pool.tail = oid - 1;
	pkt_pool.error = 0;

}


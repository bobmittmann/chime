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
 * This file implements a safe shared objects pool.
 *
 */

#define __CHIME_I__
#include "chime-i.h"

#include <stdio.h>
#include <assert.h>
#include "objpool.h"

#define __OID_VOID -1

/* metadata for the objects */
struct obj_meta {
	uint16_t ref; /* reference counter */
	uint16_t oid; /* object id */
} __attribute__((aligned(4)));

#define SIZEOF_META sizeof(struct obj_meta)
#define META_OFFS (SIZEOF_META / sizeof(uint32_t))


struct obj {
	struct obj_meta meta;
	union {
		uint16_t next;
		uint32_t data[OBJPOOL_OBJ_SIZE_MAX / 4];
    };
} __attribute__((aligned(4)));


struct objpool {
	uint32_t error;
	uint32_t nmemb;
	int32_t head;
	int32_t tail;
	struct obj obj[];
};

static struct  {
	char name[64];
	__mutex_t mutex;
	__shm_t shm;
	struct objpool * pool;
} obj_mgr;


int obj_oid(void * ptr)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	unsigned int oid;

	(void)pool;

	assert(pool != NULL);

	if (ptr == NULL) {
		assert(ptr != NULL);
//		WARN("Null pointer.", oid);
	}

	oid = obj->meta.oid;
	
	if (oid > pool->nmemb) {
	//	WARN("invalid oid=%d", oid);
		return -1;
	}

	assert(obj == &pool->obj[oid]);


	/* convert from internal index */
	return oid + 1;
}

void * obj_getinstance_incref(int oid)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj;
	void * ret;

	assert(pool != NULL);
	assert(oid > 0);

	/* XXX: convert to internal index */
	oid--;

	WARN("oid=%d", oid + 1);

	/* get instance */
	obj = &pool->obj[oid];

	__mutex_lock(obj_mgr.mutex);

	//assert(obj->meta.ref > 0);
	/* sanity check */
	if (obj->meta.ref == 0) {
		WARN("oid=%d", oid + 1);
		ret = NULL;
	} else {
		assert(obj->meta.oid == oid);

		/* increment object reference */
		obj->meta.ref++;

		ret = (void *)obj->data;
	}

	__mutex_unlock(obj_mgr.mutex);

	return ret;
}

void * obj_getinstance(int oid)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj;

	assert(pool != NULL);
	assert(oid > 0);
	/* XXX: convert to internal index */
	oid--;

	WARN("oid=%d", oid + 1);
	
	/* get instance */
	obj = &pool->obj[oid];

	/* sanity check */
	assert(obj->meta.ref > 0);
	assert(obj->meta.oid == oid);

	return (void *)obj->data;
}

void * obj_alloc(void)
{
	struct objpool * pool = obj_mgr.pool;
	void * ret;
	int oid;

	assert(pool != NULL);

	__mutex_lock(obj_mgr.mutex);

	if ((oid = pool->head) != __OID_VOID) {
		struct obj * obj;
		obj = &pool->obj[oid];

		assert(obj->meta.oid == oid);
		if (obj->meta.ref != 0) {
			WARN("[%d].meta.ref=%d.", obj->meta.oid, (int16_t)obj->meta.ref);
			DBG("opid=%d.", pool->head);
			ret = NULL;
		} else {	

			DBG3("head=%d.", pool->head);

			if (pool->head == pool->tail)
				pool->head = pool->tail = __OID_VOID; /* Pool is empty */
			else
				pool->head = obj->next;

			DBG3("head=%d.", pool->head);

			obj->meta.ref = 1; /* initialize reference counter */

			DBG("oid=%d.", oid + 1);
			ret = (void *)obj->data;
		}
	} else {
		pool->error++;
		ret = NULL;
	}

	__mutex_unlock(obj_mgr.mutex);

	return ret;
}

int obj_incref(void * ptr)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int ret;

	(void)pool;
	assert(pool != NULL);
	assert(ptr != NULL);

	WARN("oid=%d", obj->meta.oid + 1);

	__mutex_lock(obj_mgr.mutex);

	assert(obj->meta.ref > 0);

	ret = obj->meta.ref++;

	__mutex_unlock(obj_mgr.mutex);

	return ret;
}

int obj_decref(void * ptr)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int oid;
	int ret;
	int ref;

	assert(pool != NULL);
	assert(ptr != NULL);

	__mutex_lock(obj_mgr.mutex);

	oid = obj->meta.oid;

	WARN("oid=%d", oid + 1);

	assert(obj == &pool->obj[oid]);
	assert(obj->meta.ref > 0);

	DBG3("oid=%d ref=%d.", (oid + 1), obj->meta.ref);

	ref = obj->meta.ref;
	if (ref > 0) {
		if (--ref == 0) { 
			DBG("oid=%d free.", (oid + 1));
			if (pool->head == __OID_VOID) 
				pool->head = oid;
			else {
				pool->obj[pool->tail].next = oid;
			}
			pool->tail = oid;
		}
		/* write back */
		obj->meta.ref = ref;
		ret = ref;
	} else {
		/* this object is gone already!!! */
		ret = -1;
	}

	__mutex_unlock(obj_mgr.mutex);

	return ret;
}

int obj_release(int oid)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj;
	int ret;
	int ref;

	assert(pool != NULL);
	assert(oid > 0);
	/* XXX: convert to internal index */
	oid--;

	obj = &pool->obj[oid];

	__mutex_lock(obj_mgr.mutex);

	DBG("oid=%d ref=%d.", (oid + 1), obj->meta.ref);

	assert(obj->meta.oid == oid);
	assert(obj->meta.ref > 0);

	ref = obj->meta.ref;
	if (ref > 0) {
		if (--ref == 0) { 
			DBG3("oid=%d free.", (oid + 1));
			if (pool->head == __OID_VOID) 
				pool->head = oid;
			else {
				pool->obj[pool->tail].next = oid;
			}
			pool->tail = oid;
		}
		/* write back */
		obj->meta.ref = ref;
		ret = ref;
	} else { 
		/* this object is gone already!!! */
		ret = -1;
	}

	__mutex_unlock(obj_mgr.mutex);

	return ret;
}

void obj_free(void * ptr)
{
	struct objpool * pool = obj_mgr.pool;
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);
	int oid;

	assert(pool != NULL);
	assert(ptr != NULL);

	__mutex_lock(obj_mgr.mutex);

	oid = obj->meta.oid;

	assert(obj->meta.ref == 1);

	DBG3("oid=%d free.", (oid + 1));

	obj->meta.ref = 0;
	if (pool->head == __OID_VOID) 
		pool->head = oid;
	else {
		pool->obj[pool->tail].next = oid;
	}
	pool->tail = oid;

	__mutex_unlock(obj_mgr.mutex);
}

void obj_clear(void * ptr)
{
	struct obj * obj = (struct obj *)((uint32_t *)ptr - META_OFFS);

	assert(ptr != NULL);

	__mutex_lock(obj_mgr.mutex);

	assert(obj->meta.ref != 0);

	memset(obj->data, 0, OBJPOOL_OBJ_SIZE_MAX); 

	__mutex_unlock(obj_mgr.mutex);
}

static void objpool_init(struct objpool * pool, size_t nmemb)
{
	struct obj * obj;
	int oid;

	for (oid = 0; oid < nmemb; ++oid) {
		obj = &pool->obj[oid];
		DBG4("oid=%d obj=%p", oid, obj);
		obj->meta.oid = oid;
		obj->meta.ref = 0;
		obj->next = oid + 1;
	}

	pool->head = 0;
	pool->tail = oid - 1;
	pool->error = 0;
	pool->nmemb = nmemb;

	DBG1("nmemb=%d head=%d tail=%d", (int)nmemb, pool->head, pool->tail);
}


/* Allocate a new named object pool of 'nmemb' elements. */
int objpool_create(const char * name, size_t nmemb)
{
	size_t objsz = sizeof(struct obj);
	size_t size;
	int ret;

	DBG3("%d == %d!", (int)sizeof(struct obj), (int)OBJPOOL_OBJ_SIZE_MAX + 4);

	assert(sizeof(struct obj) == (OBJPOOL_OBJ_SIZE_MAX + 4));
	size = sizeof(struct obj) + (nmemb * objsz);

	strcpy(obj_mgr.name, name);

	/* remove posibly existing files from the filesystem */
	__mutex_unlink(obj_mgr.name);
	__shm_unlink(obj_mgr.name);

	if ((ret = __mutex_create(&obj_mgr.mutex, obj_mgr.name)) < 0) {
		ERR("__mutex_create(\"%s\") failed: %s!", obj_mgr.name, __strerr());
		return ret;
	}

	if ((ret = __shm_create(&obj_mgr.shm, obj_mgr.name, size)) < 0) {
		ERR("__shm_create(\"%s\") failed: %s!", obj_mgr.name, __strerr());
		return ret;
	}

	if ((obj_mgr.pool = __shm_mmap(obj_mgr.shm)) == NULL) {
		ERR("__shm_mmap() failed: %s!", __strerr());
		return -1;
	}

	DBG1("obj_mgr.pool=%p size=%d", obj_mgr.pool, (int)size);

	objpool_init(obj_mgr.pool, nmemb);

	return 0;
}

/* Open an existing named object pool. */
int objpool_open(const char * name)
{
	strcpy(obj_mgr.name, name);

	if (__mutex_open(&obj_mgr.mutex, obj_mgr.name) < 0) {
		ERR("__mutex_open(\"%s\") failed!", obj_mgr.name);
		return -1;
	}

	if (__shm_open(&obj_mgr.shm, obj_mgr.name) < 0) {
		ERR("__shm_open(\"%s\") failed!", obj_mgr.name);
		return -1;
	}

	if ((obj_mgr.pool = __shm_mmap(obj_mgr.shm)) == NULL) {
		ERR("__shm_mmap() failed!");
		return -1;
	}

	return 0;
}

void objpool_close(void)
{
	__shm_munmap(obj_mgr.shm, obj_mgr.pool);
	obj_mgr.pool = NULL;

	__shm_close(obj_mgr.shm);
	__mutex_close(obj_mgr.mutex);
}

void objpool_lock(void)
{
	__mutex_lock(obj_mgr.mutex);
}

void objpool_unlock(void)
{
	__mutex_unlock(obj_mgr.mutex);
}

void objpool_destroy(void)
{
	__shm_unlink(obj_mgr.name);
	__mutex_unlink(obj_mgr.name);
}

int objpool_get_free(void)
{
	struct objpool * pool = obj_mgr.pool;
	unsigned int n = 0;
	struct obj * obj;
	int oid;

	__mutex_lock(obj_mgr.mutex);
	
	oid = pool->head;
	if (oid != __OID_VOID) {
		n++;
		while (oid != pool->tail) {
			obj = &pool->obj[oid];
			oid = obj->next;
			n++;
		}
	}

	__mutex_unlock(obj_mgr.mutex);

	return n;
}

int objpool_get_alloc(void)
{
	struct objpool * pool = obj_mgr.pool;
	return pool->nmemb - objpool_get_free();
}


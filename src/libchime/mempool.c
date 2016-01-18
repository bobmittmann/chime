/* 
 * File:	 mempool.c
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

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef _WIN32_WINNT 
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#endif

struct mempool;

#define SIZEOF_META (__SIZEOF_POINTER__ * 2)
#define META_OFFS (SIZEOF_META / sizeof(uint64_t))

struct block_meta {
	union {
		struct {
			struct mempool * pool;
			uint32_t ref;
		};
		
		uint64_t res[SIZEOF_META / 8];
	};
};

struct block {
	struct block_meta meta;
	union {
		struct block * next;
		uint64_t data[1];
    };
};

struct mempool {
	union {
		struct {
			uint32_t error;
			struct block * free;
#ifdef BFIN
			volatile uint32_t * spinlock;
#else
#ifdef _WIN32
			CRITICAL_SECTION spinlock;
#else
			pthread_spinlock_t spinlock;
#endif
#endif
		};
		/* dummy field to ensure 64bits alignment */
		uint64_t dummy[2];
	};
	uint64_t buf[0];
};

#ifdef BFIN

/* ------------------------------------------------------------------
 * XXX: Non portable code
 * ------------------------------------------------------------------
 */

static void __spinlock_create(volatile uint32_t ** lock)
{
    uint32_t * ptr;
	ptr = sram_alloc(sizeof(uint32_t), L1_DATA_SRAM);
	if (ptr == NULL) {
		fprintf(stderr, "%s: sram_alloc() failed: %s.\n", 
				__func__, strerror(errno));

		return;
	}
	*lock = ptr;
	**lock = 0;
}

static void __spinlock_destroy(volatile uint32_t * lock)
{
	sram_free((uint32_t *)lock);
}

static inline void __spin_lock(volatile uint32_t * lock)
{
	while (bfin_atomic_xchg32((uint32_t *)lock, 1)) {
		 sched_yield();
	}
}

static inline void __spin_unlock(volatile uint32_t * lock)
{
	*lock = 0;
}

#else

#ifdef _WIN32

static inline void __spinlock_create(CRITICAL_SECTION * spinlock)
{
	InitializeCriticalSectionAndSpinCount(spinlock, 0);
}

static void __spinlock_destroy(CRITICAL_SECTION * spinlock)
{
	DeleteCriticalSection(spinlock);
}

static inline void __spin_lock(CRITICAL_SECTION * spinlock)
{
	EnterCriticalSection(spinlock);
}

static inline void __spin_unlock(CRITICAL_SECTION * spinlock)
{
	LeaveCriticalSection(spinlock);
}

#else

static inline void __spinlock_create(pthread_spinlock_t * spinlock)
{
	pthread_spin_init(spinlock, PTHREAD_PROCESS_PRIVATE);
}

static inline void __spin_lock(pthread_spinlock_t * spinlock)
{
	pthread_spin_lock(spinlock);
}

static inline void __spin_unlock(pthread_spinlock_t * spinlock)
{
	pthread_spin_unlock(spinlock);
}

static void __spinlock_destroy(pthread_spinlock_t * spinlock)
{
	pthread_spin_destroy(spinlock);
}
#endif

#endif

void * memblk_alloc(struct mempool * pool)
{
	struct block * blk;
	void * ret = NULL;

	__spin_lock(&pool->spinlock);

	if ((blk = pool->free) != NULL) {
		pool->free = blk->next;

		assert(blk->meta.ref == 0);
		assert(blk->meta.pool == pool);

		blk->meta.ref = 1; /* initialize reference counter */

		ret = (void *)blk->data;
	} else {
		pool->error++;
	}

	__spin_unlock(&pool->spinlock);

	return ret;
}

void memblk_incref(void * ptr)
{
	struct block * blk = (struct block *)((uint64_t *)ptr - META_OFFS);
	struct mempool * pool = blk->meta.pool;

	__spin_lock(&pool->spinlock);

	assert(blk->meta.ref > 0);

	blk->meta.ref++;

	__spin_unlock(&pool->spinlock);


}

void memblk_decref(void * ptr)
{
	struct block * blk = (struct block *)((uint64_t *)ptr - META_OFFS);
	struct mempool * pool = blk->meta.pool;

	__spin_lock(&pool->spinlock);

	assert(blk->meta.ref > 0);

	if (--blk->meta.ref == 0) { 
		blk->next = pool->free;
		pool->free = blk;
	} 

	__spin_unlock(&pool->spinlock);
}

bool memblk_free(struct mempool * pool, void * ptr)
{
	struct block * blk = (struct block *)((uint64_t *)ptr - META_OFFS);
	bool ret = false;

	__spin_lock(&pool->spinlock);

	assert(blk->meta.ref > 0);
	assert(blk->meta.pool == pool);

	if (--blk->meta.ref == 0) { 
		blk->next = pool->free;
		pool->free = blk;
		ret = true;
	}

	__spin_unlock(&pool->spinlock);

	return ret;
}

/* block length */
static inline size_t __blklen(size_t size) {
	return ((size + (sizeof(uint64_t) - 1)) / sizeof(uint64_t) + META_OFFS) * 
		sizeof(uint64_t);
}

void mempool_init(struct mempool * pool, size_t nmemb, size_t size)
{
	struct block * blk = (struct block *)pool->buf;
	int d;
	int i;
	int j;

	j = 0;
	d = __blklen(size) / sizeof(uint64_t);
	pool->free = blk;
	for (i = 0; i < nmemb - 1; i++) {
		j += d;
		blk->meta.pool = pool;
		blk->meta.ref = 0;
		blk->next = (struct block *)&pool->buf[j];
		blk = blk->next;
	}

	blk->meta.pool = pool;
	blk->meta.ref = 0;
	blk->next = NULL;
	pool->error = 0;
	__spinlock_create(&pool->spinlock);
}


/* Allocate a new memory pool of 'nmemb' elements of 'size' bytes. */
/* The members are 64bits aligned */
struct mempool * mempool_alloc(size_t nmemb, size_t size)
{
	struct mempool * pool;
	size_t blklen;

	assert(sizeof(struct block_meta) == SIZEOF_META);

	blklen = __blklen(size);

	pool = (struct mempool *)malloc(sizeof(struct mempool) + (nmemb * blklen));

	mempool_init(pool, nmemb, size);

	return pool;
}

void mempool_free(struct mempool * pool)
{
	__spinlock_destroy(&pool->spinlock);
}


/*****************************************************************************
 * Flexnet 
 *****************************************************************************/

#ifndef __FX_SPINLOCK_H__
#define __FX_SPINLOCK_H__

#include <stdint.h>
#include <assert.h>

#ifdef BFIN

typedef uint32_t * __spinlock_t;

/* ------------------------------------------------------------------
 * XXX: Non portable code
 * ------------------------------------------------------------------
 */

static inline void __spinlock_create(volatile __spinlock_t * spinlock) {
    spinlock_t ptr = sram_alloc(sizeof(uint32_t), L1_DATA_SRAM);
	assert (ptr != NULL);
	*spinlock = ptr;
	**spinlock = 0;
}

static inline void __spinlock_destroy(volatile __spinlock_t spinlock) {
	sram_free((uint32_t *)spinlock);
}

static inline void __spin_lock(volatile __spinlock_t spinlock) {
	while (bfin_atomic_xchg32((uint32_t *)spinlock, 1))
		 sched_yield();
}

static inline void __spin_unlock(volatile __spinlock_t spinlock) {
	*spinlock = 0;
}

static inline void __spinlock_destroy(__spinlock_t spinlock) {
    sram_free(spinlock);
}

#else

#if defined(_WIN32) 

#define _WIN32_WINNT 0x0600
#include <windows.h>

typedef CRITICAL_SECTION * __spinlock_t;

static inline void __spinlock_create(__spinlock_t * spinlock) {
    __spinlock_t ptr = malloc(sizeof(CRITICAL_SECTION));
	assert (ptr != NULL);
	*spinlock = ptr;
	InitializeCriticalSectionAndSpinCount(*spinlock, 0);
}

static inline void __spin_lock(__spinlock_t spinlock) {
	EnterCriticalSection(spinlock);
}

static inline void __spin_unlock(__spinlock_t spinlock) {
	LeaveCriticalSection(spinlock);
}

static inline void __spinlock_destroy(__spinlock_t spinlock) {
	DeleteCriticalSection(spinlock);
    free((void *)spinlock);
}

#else

#include <pthread.h>
#include <stdlib.h>

typedef pthread_spinlock_t * __spinlock_t;

static inline void __spinlock_create(__spinlock_t * spinlock) {
    __spinlock_t ptr = malloc(sizeof(pthread_spinlock_t));
	assert (ptr != NULL);
	*spinlock = ptr;
	pthread_spin_init(*spinlock, PTHREAD_PROCESS_PRIVATE);
}

static inline void __spin_lock(__spinlock_t spinlock) {
	pthread_spin_lock(spinlock);
}

static inline void __spin_unlock(__spinlock_t spinlock) {
	pthread_spin_unlock(spinlock);
}

static inline void __spinlock_destroy(__spinlock_t spinlock) {
	pthread_spin_destroy(spinlock);
    free((void *)spinlock);
}
#endif
#endif



#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif	

#endif /* __FX_SPINLOCK_H__ */


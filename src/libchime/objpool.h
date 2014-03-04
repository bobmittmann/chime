/*****************************************************************************
 * Object pool public header file
 *****************************************************************************/

#ifndef __OBJPOOL_H__
#define __OBJPOOL_H__

#include <stdint.h>

#define OID_NULL 0

#define OBJPOOL_OBJ_SIZE_MAX (1024 - 4)

#ifdef __cplusplus
extern "C" {
#endif

void * obj_alloc(void);;

int obj_incref(void * ptr);

int obj_decref(void * ptr);

void obj_free(void * ptr);

int obj_oid(void * ptr);

void obj_clear(void * ptr);

void * obj_getinstance(int oid);

void * obj_getinstance_incref(int oid);

int obj_release(int oid);

int objpool_create(const char * name, size_t nmemb);

void objpool_destroy(void);

int objpool_open(const char * name);

void objpool_close(void);

void objpool_lock(void);

void objpool_unlock(void);

int objpool_get_free(void);

int objpool_get_alloc(void);

#ifdef __cplusplus
}
#endif	

#endif /* __OBJPOOL_H__ */


/* 
 * File:   cache.h
 * Author: dhana
 *
 * Created on January 29, 2016, 2:30 PM
 */

#ifndef CACHE_H
#define	CACHE_H
#include "types.h"
#include "object.h"

#ifdef	__cplusplus
extern "C" {
#endif


struct cache_obj_ops {
	int (*obj_read)(char *id, struct object **robjp);
	int (*obj_write)(struct object *obj);
};

typedef void * cache_obj;
cache_obj cache_init(int num_cache_slots, struct cache_obj_ops *ops);
struct object * cache_read(cache_obj cache, char *id);
int cache_write(cache_obj cache, struct object *obj);
int cache_invalidate(cache_obj cache, char *id);
void cache_print_slots(cache_obj cache, int num_slots);

#ifdef	__cplusplus
}
#endif

#endif	/* CACHE_H */


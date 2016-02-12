/* 
 * File:   main.c
 * Author: dhana
 *
 * Created on January 29, 2016, 1:32 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include "object.h"
#include "cache.h"
#include "clock_pro.h"

/*
 * 
 */
int
cache_read_stub(char *id, struct object **robjp)
{
	return create_object(id, robjp);
}

int
cache_write_stub(struct object *obj)
{
	return 0;
}

int
validate_cache_read(char *id, struct object *obj)
{
	if (strcmp(id, obj->id) != 0) {
		printf("ERROR: Read %s, Got %s\n", id, obj->id);
		return FAILURE;
	}
	return SUCCESS;
}

int 
main(int argc, char** argv) 
{
	int ret, i;
	int num_slots = 10;
	char id[16];
	struct cache_obj_ops cops = {cache_read_stub, cache_write_stub};
	struct cache *cache;
	struct object *obj;
	cache = cache_init(num_slots, &cops);
	if (!cache) {
		printf("cache init failed\n");
		return EXIT_FAILURE;
	}
	printf("Cache initialized with %d entries.\n", num_slots);

	// Fill the cache.
	for (i = 0; i < num_slots; i++) {
		snprintf(id, sizeof(id), "object-%d",i); 
		obj = cache_read(cache, id);
		ret = validate_cache_read(id, obj);
		if (ret < 0) {
			assert(0);
		}
		cache_print_slots(cache, num_slots);
	}
	// Add more.
	for (i = 0; i < 5; i++) {
		snprintf(id, sizeof(id), "object-%d", num_slots + i); 
		obj = cache_read(cache, id);
		ret = validate_cache_read(id, obj);
		if (ret < 0) {
			assert(0);
		}
		cache_print_slots(cache, num_slots);
	}
	// The first few slots again
	for (i = 0; i < 4; i++) {
		snprintf(id, sizeof(id), "object-%d", i); 
		obj = cache_read(cache, id);
		ret = validate_cache_read(id, obj);
		if (ret < 0) {
			assert(0);
		}
		cache_print_slots(cache, num_slots);
	}

	// Fill up the clock
	for (i = 5; i < 15; i++) {
		snprintf(id, sizeof(id), "object-%d", num_slots + i); 
		obj = cache_read(cache, id);
		ret = validate_cache_read(id, obj);
		if (ret < 0) {
			assert(0);
		}
		cache_print_slots(cache, num_slots);
	}


	return (EXIT_SUCCESS);
}


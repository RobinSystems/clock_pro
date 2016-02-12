#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "clock_pro.h"

// An object ptr in this array tells us that the object has been written to the cache slot.

struct cache_node {
	void *object;
	int free;
};

struct cache {
	uint32 num_slots;
	struct cache_node *cache_map;
	struct cache_node *free_slots;
	struct cache_obj_ops ops;
	struct clock_data *clock;
};

static uint32
cache_get_free_slot(struct cache *cache)
{
	struct cache_node *ret = cache->free_slots->object;
	if (ret) {
		cache->free_slots->object = ret->object;
		ret->free = 0;
		ret->object = NULL;
		return ret - cache->cache_map;
	}
	return 0;
}

static void
cache_add_free_slot(struct cache *cache, struct cache_node *node)
{
	node->free = 1;
	node->object = cache->free_slots->object;
	cache->free_slots->object = node;
}

static int
cache_lookup(struct cache *cache, char *id, 
		struct cache_node **nodep, uint32 *clock_slotp)
{
	int ret;
	ret = clock_lookup(cache->clock, id, (void **)nodep, clock_slotp);
	return ret;
}

static int
cache_add_object(struct cache *cache, struct object *obj)
{
	int ret;
	uint32 clock_slot, cache_slot;
	struct cache_node *node;
	ret = cache_lookup(cache, obj->id, &node, &clock_slot);
	if (!ret) {
		if (node) {
			printf("Object %s is already in the cache. <%p, %lu, %u>\n",
				obj->id, node, node - cache->cache_map, clock_slot);
			return FAILURE;
		}
	}

	cache_slot = cache_get_free_slot(cache);
	if (!cache_slot) {
		clock_handle_fault(cache->clock, (void **)&node);
		struct object *cur = node->object;
		cache_slot = node - cache->cache_map;
		printf("Replacing object %s with object %s at slot %d\n",
			cur->id, obj->id, cache_slot);
		destroy_object(cur);
	} else {
		printf("Inserting object %s at slot %d\n", obj->id, cache_slot);
	}
	if (cache_slot) {
		cache->cache_map[cache_slot].object = obj;
		ret = clock_add_object(cache->clock, obj->id, 
					&cache->cache_map[cache_slot]);
		return ret;
	}
	printf("Failed to find a cache slot for object %s\n", obj->id);
	return FAILURE;
}

/* Called when an object is invalidated */
static int
cache_remove_object(struct cache *cache, char *id)
{
	int ret;
	uint32 clock_slot;
	struct cache_node *cache_node;
	ret = cache_lookup(cache, id, &cache_node, &clock_slot);
	if (ret != SUCCESS) {
		printf("Object %s is not in the cache.\n", id);
		return FAILURE;
	}
	if (cache_node) {
		printf("Removing object %s at slot %lu (clock slot %u)\n", id, 
			cache_node - cache->cache_map, clock_slot);
		cache_add_free_slot(cache, cache_node);
	}
	
	clock_evict_cache_node(cache->clock, clock_slot);
	return ret;
}

static void
cache_mark_accessed(struct cache *cache, char *id, struct cache_node *node, 
		uint32 clock_slot)
{
	clock_mark_accessed(cache->clock, id, clock_slot);
	return;
}

void
cache_print_node(struct cache *cache, struct cache_node *node)
{
	printf("%lu:", (node - cache->cache_map));	

	if (!node->free) {
		print_object(node->object);
		printf("\n");
	} else {
		printf("Free\n");
	} 
}

void
cache_print_slots(cache_obj cobj, int num_slots)
{
	struct cache *cache = (struct cache *) cobj;
	int i;
	for (i = 1; i <= num_slots; i++) {
		if (!cache->cache_map[i].free) {
			cache_print_node(cache, &cache->cache_map[i]);
		}
	}
	clock_print_slots(cache->clock);

}

void *
cache_init(int num_cache_slots, struct cache_obj_ops *obj_ops)
{
	int ret, i;
	struct cache *cache = malloc(sizeof(struct cache));
	if (!cache) {
		printf("Failed to allocate memory (%lu bytes) for cache obj\n", 
			sizeof(struct cache));
		return NULL;
	}
	cache->cache_map = (struct cache_node *) calloc(num_cache_slots + 1, 
						sizeof(struct cache_node));
	if (!cache->cache_map) {
		free(cache);
		printf("Failed to allocate memory for %d cache nodes\n", 
			num_cache_slots);
		return NULL;
	}
	cache->free_slots = &cache->cache_map[0];
	for (i = 0; i < num_cache_slots; i++) {
		cache->cache_map[i].free = 1;
		cache->cache_map[i].object = &cache->cache_map[i + 1];
	}
	cache->cache_map[num_cache_slots].free = 1;
	cache->cache_map[num_cache_slots].object = NULL;
	cache->clock = clock_init(num_cache_slots);
	if (!cache->clock) {
		free(cache->cache_map);
		free(cache);
		printf("Failed to initialize clock with %d nodes\n", 
			num_cache_slots);
		return NULL;
	}
	cache->ops = *obj_ops;
	return cache;
}

struct object *
cache_read(cache_obj cobj, char *id)
{
	struct cache *cache = (struct cache *)cobj;
	struct cache_node *node;
	struct object *obj;
	uint32 clock_slot;
	int ret;
	ret = cache_lookup(cache, id, &node, &clock_slot);
	if (!ret) {
		cache_mark_accessed(cache, id, node, clock_slot);
		if (node) {
			obj = node->object;
			return obj;
		} 
	}
	// Cache miss case. Read the object and add it to the cache.
	int rstatus;
	rstatus = cache->ops.obj_read(id, &obj);
	if (!rstatus) {
		cache_add_object(cache, obj);
	}
	return obj;
}

int
cache_write(cache_obj cobj, struct object *obj)
{
	struct cache *cache = (struct cache *)cobj;
	struct cache_node *node;
	uint32 clock_slot;
	int ret;
	ret = cache_lookup(cache, obj->id, &node, &clock_slot);
	if (!ret) {
		cache_mark_accessed(cache, obj->id, node, clock_slot);
	} else {
		// Cache miss case. Read the object and add it to the cache.
		int rstatus;
		rstatus = cache->ops.obj_write(obj);
		if (!rstatus) {
			cache_add_object(cache, obj);
		}
	}
}

int
cache_invalidate(cache_obj cobj, char *id)
{
	struct cache *cache = (struct cache *)cobj;
	struct cache_node *node;
	uint32 clock_slot;
	int ret;
	ret = cache_lookup(cache, id, &node, &clock_slot);
	if (ret < 0) {
		return ret;
	}
	ret = cache_remove_object(cache, id);
	return ret;
}
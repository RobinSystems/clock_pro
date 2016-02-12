/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   clock_pro.h
 * Author: dhana
 *
 * Created on February 1, 2016, 11:32 AM
 */

#ifndef CLOCK_PRO_H
#define CLOCK_PRO_H

#include <glib.h>
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct clock_node {
	struct clock_node *prev;
	struct clock_node *next;
	void	*cache_node;
	char	*id;
	uint16	refcnt;	
	uint8	hot;
	uint8	in_test;
};

struct clock_data {
	struct clock_node *hot_hand;
	struct clock_node *cold_hand;
	struct clock_node *test_hand;
	struct clock_node *clock_nodes;
	struct clock_node free_nodes;
	GHashTable *clock_object_hash;
	uint32 nonresident_cold_pages;
	uint32 max_nonresident_cold_pages;
};
	
struct clock_data *clock_init(int cache_slots);
void clock_handle_fault(struct clock_data *clock, void **cache_nodep);
int clock_add_object(struct clock_data *clock, char *id, void *cache_node);
void clock_evict_cache_node(struct clock_data *clock, uint32 clock_slot);
void clock_print_node(struct clock_data *clock, struct clock_node *node);
void clock_print_slots(struct clock_data *clock);
int clock_lookup(struct clock_data *clock, char *id, void **cache_slotp, uint32 *clock_slotp);


#ifdef __cplusplus
}
#endif

#endif /* CLOCK_PRO_H */


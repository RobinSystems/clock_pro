#include <stdio.h>
#include <string.h>
#include "clock_pro.h"
#include "cache.h"

struct clock_hash_value {
	uint32 cache_slot;
	uint32 clock_slot;
};

void
clock_id_destroy_func(void *id)
{
	printf("Key %s evicted from hash table\n", (char *)id);
}

gboolean
clock_id_cmp(gconstpointer id1, gconstpointer id2)
{
	int ret = strcmp((char *)id1, (char *)id2);
	return (ret == 0);
}

struct clock_data *
clock_init(int cache_size)
{
	int i, num_nodes = 2 * cache_size;
	struct clock_data *clock = malloc(sizeof(struct clock_data));
	if (!clock) {
		printf("Failed to allocate %lu bytes for clock data\n",
			sizeof(struct clock_data));
		return NULL;

	}

	clock->clock_object_hash = g_hash_table_new_full(g_str_hash, clock_id_cmp, 
						clock_id_destroy_func, NULL);
	if (!clock->clock_object_hash) {
		printf("Failed to allocate clock hash table\n");
		free(clock);
		return NULL;
	}
	clock->clock_nodes = (struct clock_node *) calloc(num_nodes,
					sizeof(struct clock_node));
	if (!clock->clock_nodes) {
		printf("Failed to allocate memory for %d clock->clock_nodes\n",
			num_nodes);
		free(clock->clock_object_hash);
		free(clock);
		return NULL;
	}

	for (i = 1; i < num_nodes - 1; i++) {
		clock->clock_nodes[i].next = &clock->clock_nodes[i + 1];
		clock->clock_nodes[i].prev = &clock->clock_nodes[i - 1];
	}
	clock->free_nodes.next = &clock->clock_nodes[0];
	clock->free_nodes.prev = &clock->clock_nodes[num_nodes - 1];
	clock->clock_nodes[0].next = &clock->clock_nodes[1];
	clock->clock_nodes[0].prev = &clock->free_nodes;
	clock->clock_nodes[num_nodes - 1].next = &clock->free_nodes;
	clock->clock_nodes[num_nodes - 1].prev = &clock->clock_nodes[num_nodes - 2];
	clock->cold_hand = NULL;
	clock->hot_hand = NULL;
	clock->test_hand = NULL;
	clock->max_nonresident_cold_pages = cache_size;

	return clock;
}

static inline struct clock_node *
clock_get_next(struct clock_node *node)
{
	return node->next;
}

static inline struct clock_node *
clock_get_node(struct clock_data *clock, uint32 clock_slot)
{
	return &clock->clock_nodes[clock_slot];
}

static inline uint32
clock_get_slot(struct clock_data *clock, struct clock_node *node)
{
	return node - clock->clock_nodes;
}

static inline int
clock_is_hot(struct clock_node *node)
{
	return node->hot;
}

static inline void
clock_mark_hot(struct clock_data *clock, struct clock_node *node)
{
	printf("Marking clock slot %u <%s, %p> as hot\n", 
		clock_get_slot(clock, node), node->id, node->cache_node);
	node->hot = 1;
	if (!clock_is_hot(clock->hot_hand)) {
		clock->hot_hand = node;
	}
}

static inline void
clock_mark_cold(struct clock_data *clock, struct clock_node *node)
{
	printf("Marking clock slot %u <%s, %p> as cold\n",
		clock_get_slot(clock, node), node->id, node->cache_node);
	node->hot = 0;
}

static inline void
clock_set_in_test(struct clock_data *clock, struct clock_node *node)
{
	assert(!clock_is_hot(node));
	printf("Marking clock slot %u <%s, %p> as in test\n", 
		clock_get_slot(clock, node), node->id, node->cache_node);
	node->in_test = 1;
}

static inline int
clock_in_test(struct clock_node *node)
{
	return node->in_test;
}

static inline void
clock_reset_in_test(struct clock_data *clock, struct clock_node *node)
{
	assert(!clock_is_hot(node));
	printf("Marking clock slot %u <%s, %p> as out of test\n", 
		clock_get_slot(clock, node), node->id, node->cache_node);
	node->in_test = 0;
}

static inline void
clock_set_nonresident(struct clock_data *clock, struct clock_node *node)
{
	printf("Setting node %u id %s to be non resident\n", 
		clock_get_slot(clock, node), node->id);
	node->cache_node = NULL;
	if (!clock->test_hand) {
		clock->test_hand = node;
	}
}

static inline int
clock_is_resident(struct clock_node *node)
{
	return (node->cache_node != NULL);
}

static inline void
clock_remove_node(struct clock_node *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = node->prev = node;
}

static inline void
clock_insert_node(struct clock_node *node, struct clock_node *pnode)
{
	node->next = pnode->next;
	node->prev = pnode;
	pnode->next->prev = node;
	pnode->next = node;
};

static inline void
clock_free_node(struct clock_data *clock, struct clock_node *node)
{
	clock_remove_node(node);
	assert(node->id);
	free(node->id);
	node->id = NULL;
	clock_insert_node(node, &clock->free_nodes);
}

/* 
 * If the hot hand encounters a cold page, it will
 * 1. Terminate the page's test period.
 * 2. If the page is not resident, remove it from the clock.
 */
static struct clock_node *
clock_hot_hand_cold_page_action(struct clock_data *clock, struct clock_node *node)
{
	struct clock_node *next;
	assert(!clock_is_hot(node));
	next = clock_get_next(node);
	clock_reset_in_test(clock, node);
	if (clock->test_hand == node) {
		if (clock->nonresident_cold_pages) {
		// Find the next nonresident cold page in test.
			for (clock->test_hand = next; 
				!clock_is_hot(clock->test_hand) && 
				clock_in_test(clock->test_hand) &&
				!clock_is_resident(clock->test_hand);
				clock->test_hand = clock_get_next(clock->test_hand)) {
				assert(clock->test_hand != node);
			}  
		} else {
			clock->test_hand = NULL;
		}	
	}
	if (!clock_is_resident(node)) {
		// Non resident page. Remove from clock
		clock_evict_node(node);
		clock->nonresident_cold_pages--;
	}
	return next;

}

/* 
 * If the hot hand encounters a hot page, if the page 
 * 1. Has 0 refcnt, mark it cold.
 * 2. Has non-zero refcnt, set refcnt to 0.
 */
static struct clock_node *
clock_hot_hand_hot_page_action(struct clock_data *clock, struct clock_node *node)
{
	struct clock_node *next = clock_get_next(node);
	if (node->refcnt) {
		node->refcnt = 0;
	} else {
		clock_mark_cold(clock, node);
	}
	return next;
}

static void
clock_hot_hand_actions(struct clock_data *clock)
{
	struct clock_node *list_tail = clock->hot_hand;
	/*
	 * Find a hot page with refcnt of 0. Note that if the clock->hot_hand points to
	 * only hot page in the cache, and the hot page has a refcnt > 0, in the 
	 * first round it will clear the page's refcnt and in the second round
	 * it will mark it cold.
	 */
	while ((!clock_is_hot(clock->hot_hand) || clock->hot_hand->refcnt != 0)) {
		/* If the page is a cold page*/
		if (!clock_is_hot(clock->hot_hand))  {
			clock->hot_hand = clock_hot_hand_cold_page_action(clock, 
								clock->hot_hand);
		} else {
			clock->hot_hand = clock_hot_hand_hot_page_action(clock,
								clock->hot_hand);
		}
	}
	if (clock_is_hot(clock->hot_hand)) {
		assert(!clock->hot_hand->refcnt);
		clock->hot_hand = clock_hot_hand_hot_page_action(clock,
								clock->hot_hand);
	}
	while (!clock_is_hot(clock->hot_hand) && list_tail != clock->hot_hand) {
		clock->hot_hand = clock_hot_hand_cold_page_action(clock, 
								clock->hot_hand);
	}
	if (!clock_is_hot(clock->hot_hand)) {
		/* No other hot pages in the clock */
		assert(clock->hot_hand == list_tail);
		clock->hot_hand = NULL;
	}
}

/*
 * When the cold hand encounters a cold page:
 * 1. If the page has non-zero refcnt 
 *    	reset its refcnt to 0.
 *       if the page is in its test period,
 *    	   then mark the page hot. Run the hot page actions.
 * 2. If the page has zero refcnt
 * 	 if the page is not in test,
 * 	    evict the page from the clock. Return the cache slot.
 *       else
 * 	    mark it evicted from the cache, keep the metadata in the clock.
 *          return the cache slot.
 */
static struct clock_node *
clock_cold_hand_cold_page_action(struct clock_data *clock, struct clock_node *node)
{
	void *ret_cache_slot = NULL;
	struct clock_node *next = clock_get_next(node);
	assert (!clock_is_hot(node));

	/*
	 * The cold page has a non-zero refcnt. Reset the refcnt.
	 * Move the page to the head of the list (after the clock->hot_hand).
	 * If the cold page was in its test period, then it just became a hot
	 * page. So mark it hot.
	 */
	if (node->refcnt) {
		node->refcnt = 0;
		clock_remove_node(node);
		clock_insert_node(node, clock->hot_hand);
		if (clock_in_test(node)) {
			clock_mark_hot(clock, node);
			clock_hot_hand_actions(clock);
		}
	} else {
		/*
		 * The cold page has zero refcnt. The data will be evicted from
		 * the cache. The metadata is kept in the cache if the page is 
		 * still in_test.
		 */
		clock_set_nonresident(clock, node);
		if (clock_in_test(node)) {
			clock->nonresident_cold_pages++;
		} else {
			clock_evict_node(clock, node);
			if (node == next) {
				next = NULL;
			}
		}
	}
	return next;
}

static void
clock_test_hand_actions(struct clock_data *clock)
{
	struct clock_node *next = NULL;
	assert(clock->test_hand);
	// Re-init the test hand.
	while (clock_is_hot(clock->test_hand)) {
		clock->test_hand = clock_get_next(clock->test_hand);
	}
	while (clock->nonresident_cold_pages >= 
		clock->max_nonresident_cold_pages) {
		next = clock_get_next(clock->test_hand);
		if (clock_in_test(clock->test_hand)) {
			clock_reset_in_test(clock, clock->test_hand);
		}
		if (!clock_is_resident(clock->test_hand)) {
			clock_free_node(clock, clock->test_hand);
			clock->nonresident_cold_pages--;
			clock->test_hand = next;
		}
		while (clock_is_hot(clock->test_hand)) {
			clock->test_hand = clock_get_next(clock->test_hand);
		}
	}
}

static void 
clock_node_info(struct clock_data *clock, struct clock_node *node, char *buf,
		int bufsz)
{
	if (node->id) {
		snprintf(buf, bufsz, "%u: %s res: %d hot %u in_test %u "
			"refcnt %u", clock_get_slot(clock, node), node->id, 
			node->cache_node != NULL ? 1: 0,
			node->hot, node->in_test, node->refcnt);
	} else {
		snprintf(buf, bufsz, "%u: free", clock_get_slot(clock, node)); 
	}

	
}

void
clock_handle_fault(struct clock_data *clock, void **cache_nodep)
{
	int run_hot_hand = 0;
	void *ret_cache_node = NULL;
	struct clock_node *victim_node = NULL;
	/*
	 * Look at the node at the cold hand. If it points to an unused node or
	 * a cold node with refcnt 0, use it.
	 */
	while ((clock_is_hot(clock->cold_hand) || clock->cold_hand->refcnt != 0) && 
		clock->cold_hand != clock->hot_hand) {
		if (!clock_is_hot(clock->cold_hand)) {
			clock->cold_hand = clock_cold_hand_cold_page_action(
						clock, clock->cold_hand); 
		}
	}
	if (clock->cold_hand->refcnt == 0) {
		char buf[256];
		victim_node = clock->cold_hand;
		assert(victim_node->cache_node);
		*cache_nodep = victim_node->cache_node;
		clock_node_info(clock, clock->cold_hand, buf, sizeof(buf));
		printf("Selected for eviction node %s\n", buf);
		clock->cold_hand = clock_cold_hand_cold_page_action(clock, 
							clock->cold_hand);
	}
	/* If the cold hand hits a hot page, it should be pointing the hot hand */
	assert (!clock->cold_hand || clock_is_hot(clock->cold_hand) ? 
		clock->cold_hand == clock->hot_hand : 1);
	while (clock->cold_hand && clock_is_hot(clock->cold_hand)) {
		clock->cold_hand = clock_get_next(clock->cold_hand);
	}
	if (clock->nonresident_cold_pages >= clock->max_nonresident_cold_pages) {
		clock_test_hand_actions(clock);
	}
	return;
}

int
clock_add_object(struct clock_data *clock, char *id, void *cache_node)
{
	gboolean newentry;
	struct clock_node *node = g_hash_table_lookup(clock->clock_object_hash, 
							id);
	if (node) {
		assert(!node->cache_node);
		node->cache_node = cache_node;
		if (!clock_is_hot(node) && clock_in_test(node)) {
			clock_mark_hot(clock, node);
		}
		return SUCCESS;
	}

	if (clock->free_nodes.next == &clock->free_nodes) {
		printf("Out of clock free slots\n");
		assert(0);
		return FAILURE;
	}

	node = clock->free_nodes.next;
	assert(node);
	clock_remove_node(node);

	node->cache_node = cache_node;
	node->id = strdup(id);
	clock_mark_cold(clock, node);
	clock_set_in_test(clock, node);
	newentry = g_hash_table_insert(clock->clock_object_hash, node->id, node);
	if (!newentry) {
		printf("Overwrote id %s in the hash table\n", node->id);
		assert(0);
	}

	/* Add the new node at the head of the list (after the hot hand)*/
	if (clock->hot_hand) {
		clock_insert_node(node, clock->hot_hand);
	}
	/* If there is no hot hand yet, set this node as the hot hand */
	if (!clock->hot_hand || !clock_is_hot(clock->hot_hand)) {
		clock->hot_hand = node;
	}
	if (!clock->cold_hand) {
		clock->cold_hand = node;
	}
	return SUCCESS;
}

void
clock_evict_cache_node(struct clock_data *clock, uint32 clock_slot)
{
	struct clock_node *node = clock_get_node(clock, clock_slot);
	clock_set_nonresident(clock, node);
}

int 
clock_evict_node(struct clock_data *clock, struct clock_node *node)
{
	gboolean found;
	printf("Remove slot %u <%s> from clock\n", clock_get_slot(clock, node), 
		node->id);
	found = g_hash_table_remove(clock->clock_object_hash, node->id);
	assert(found);
	clock_free_node(clock, node);
	return found;
}

void
clock_mark_accessed(struct clock_data *clock, char *id, uint32 clock_slot)
{
	char buf[256];
	struct clock_node *node = clock_get_node(clock, clock_slot);
	if (!node) {
		return;
	}
	assert(clock_id_cmp(id, node->id));
	node->refcnt++;
	// Should we mark this node as hot if the node is in_test???
	clock_node_info(clock, node, buf, sizeof(buf));
	printf("Incrementing refcnt of %s\n", buf);
	return;
}

int
clock_lookup(struct clock_data *clock, char *id, void **cache_slotp, 
		uint32 *clock_slotp)
{
	struct clock_node *val = g_hash_table_lookup(clock->clock_object_hash, 
							id);
	if (!val) {
		return FAILURE;
	}
	if (cache_slotp) {
		*cache_slotp = val->cache_node;
	}
	if (clock_slotp) {
		*clock_slotp = clock_get_slot(clock, val);
	}
	return SUCCESS;
}

void
clock_print_node(struct clock_data *clock, struct clock_node *node)
{
	char buf[256];
	clock_node_info(clock, node, buf, sizeof(buf));
	printf("%s\n", buf);
}

void
clock_print_hand(struct clock_data *clock, struct clock_node *node, char *name)
{
	if (node) {
		printf("%s: %p <%u %s>\n", name, node, 
			clock_get_slot(clock, node), node->id);
	}
}
void
clock_print_slots(struct clock_data *clock)
{
	struct clock_node *cur = clock_get_next(clock->hot_hand);
	clock_print_hand(clock, clock->cold_hand, "cold_hand");
	clock_print_hand(clock, clock->hot_hand, "hot_hand");
	clock_print_hand(clock, clock->test_hand, "test_hand");

	while (cur != clock->hot_hand) {
		clock_print_node(clock, cur);
		cur = clock_get_next(cur);
	}
	clock_print_node(clock, cur);
	for (cur = clock->free_nodes.next; cur != &clock->free_nodes;
		cur = clock_get_next(cur)) {
		clock_print_node(clock, cur);
	}
}
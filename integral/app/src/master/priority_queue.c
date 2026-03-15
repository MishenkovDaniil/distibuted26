#include <stdlib.h>
#include <string.h>

#include "priority_queue.h"

typedef struct {
    void *val;
    double key;
} pqueue_node_t;

struct pqueue {
    pqueue_node_t *nodes;
    size_t size;
    size_t capacity;
};

#define INITIAL_CAPACITY 16
#define GROWTH_FACTOR 2

static void swap_nodes(pqueue_node_t *a, pqueue_node_t *b)
{
    pqueue_node_t temp = *a;
    *a = *b;
    *b = temp;
}

static size_t parent(size_t idx) { return (idx - 1) / 2; }
static size_t left_child(size_t idx) { return 2 * idx + 1; }
static size_t right_child(size_t idx) { return 2 * idx + 2; }

/* Move element up to maintain heap property */
static void sift_up(pqueue_t *pqueue, size_t idx)
{
    while (idx > 0) {
        size_t p = parent(idx);
        if (pqueue->nodes[p].key <= pqueue->nodes[idx].key)
            break;
        swap_nodes(&pqueue->nodes[idx], &pqueue->nodes[p]);
        idx = p;
    }
}

/* Move element down to maintain heap property */
static void sift_down(pqueue_t *pqueue, size_t idx)
{
    while (left_child(idx) < pqueue->size) {
        size_t smallest = idx;
        size_t l = left_child(idx);
        size_t r = right_child(idx);

        if (l < pqueue->size && pqueue->nodes[l].key < pqueue->nodes[smallest].key)
            smallest = l;
        if (r < pqueue->size && pqueue->nodes[r].key < pqueue->nodes[smallest].key)
            smallest = r;

        if (smallest == idx)
            break;

        swap_nodes(&pqueue->nodes[idx], &pqueue->nodes[smallest]);
        idx = smallest;
    }
}

pqueue_t *pqueue_ctor(void)
{
    pqueue_t *pqueue = malloc(sizeof(pqueue_t));
    if (!pqueue)
        return NULL;

    pqueue->nodes = malloc(INITIAL_CAPACITY * sizeof(pqueue_node_t));
    if (!pqueue->nodes) {
        free(pqueue);
        return NULL;
    }

    pqueue->size = 0;
    pqueue->capacity = INITIAL_CAPACITY;
    return pqueue;
}

void pqueue_dtor(pqueue_t *pqueue)
{
    if (pqueue) {
        free(pqueue->nodes);
        free(pqueue);
    }
}

void pqueue_add(pqueue_t *pqueue, void *val, double key)
{
    if (!pqueue)
        return;

    /* Expand capacity if needed */
    if (pqueue->size >= pqueue->capacity) {
        pqueue->capacity *= GROWTH_FACTOR;
        pqueue_node_t *new_nodes = realloc(pqueue->nodes,
                                           pqueue->capacity * sizeof(pqueue_node_t));
        if (!new_nodes)
            return;
        pqueue->nodes = new_nodes;
    }

    /* Add to end and restore heap property */
    pqueue->nodes[pqueue->size].val = val;
    pqueue->nodes[pqueue->size].key = key;
    sift_up(pqueue, pqueue->size);
    pqueue->size++;
}

void *pqueue_get_top(const pqueue_t *pqueue)
{
    if (!pqueue || pqueue->size == 0)
        return NULL;
    return pqueue->nodes[0].val;
}

double pqueue_get_top_key(const pqueue_t *pqueue)
{
    if (!pqueue || pqueue->size == 0)
        return -1.0;  /* Invalid key */
    return pqueue->nodes[0].key;
}

void *pqueue_del_top(pqueue_t *pqueue)
{
    if (!pqueue || pqueue->size == 0)
        return NULL;

    void *val = pqueue->nodes[0].val;

    /* Move last element to root */
    pqueue->nodes[0] = pqueue->nodes[pqueue->size - 1];
    pqueue->size--;

    /* Restore heap property */
    if (pqueue->size > 0)
        sift_down(pqueue, 0);

    return val;
}

void pqueue_del(pqueue_t *pqueue, void *val, double key)
{
    if (!pqueue)
        return;

    /* Find and remove element */
    for (size_t i = 0; i < pqueue->size; i++) {
        if (pqueue->nodes[i].val == val && pqueue->nodes[i].key == key) {
            /* Move last element to deleted position */
            pqueue->nodes[i] = pqueue->nodes[pqueue->size - 1];
            pqueue->size--;

            /* Restore heap property (either sift up or down) */
            if (i < pqueue->size) {
                if (i > 0 && pqueue->nodes[i].key < pqueue->nodes[parent(i)].key)
                    sift_up(pqueue, i);
                else
                    sift_down(pqueue, i);
            }
            return;
        }
    }
}

int pqueue_is_empty(const pqueue_t *pqueue)
{
    return !pqueue || pqueue->size == 0;
}

size_t pqueue_size(const pqueue_t *pqueue)
{
    return pqueue ? pqueue->size : 0;
}

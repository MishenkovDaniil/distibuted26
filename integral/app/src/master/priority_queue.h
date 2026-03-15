#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>

typedef struct pqueue pqueue_t;

pqueue_t *pqueue_ctor(void);
void pqueue_dtor(pqueue_t *pqueue);
void pqueue_add(pqueue_t *pqueue, void *val, double key);
void *pqueue_get_top(const pqueue_t *pqueue);
double pqueue_get_top_key(const pqueue_t *pqueue);
void *pqueue_del_top(pqueue_t *pqueue);
void pqueue_del(pqueue_t *pqueue, void *val, double key);
int pqueue_is_empty(const pqueue_t *pqueue);
size_t pqueue_size(const pqueue_t *pqueue);

#endif /* PRIORITY_QUEUE_H */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "packet.h"

/* Work Queue Item */
typedef struct work_item {
    packet_t *packet;
    struct work_item *next;
} work_item_t;

/* Thread Pool Structure */
typedef struct {
    pthread_t *threads;
    int num_threads;
    
    /* Work queue */
    work_item_t *queue_head;
    work_item_t *queue_tail;
    int queue_size;
    int max_queue_size;
    
    /* Synchronization */
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;
    
    /* Control */
    int is_running;
    int packets_processed;
} thread_pool_t;

/* Function Declarations */
thread_pool_t* thread_pool_create(int num_threads, int max_queue_size);
void thread_pool_destroy(thread_pool_t *pool);
int thread_pool_enqueue(thread_pool_t *pool, packet_t *packet);
int thread_pool_is_running(thread_pool_t *pool);
int thread_pool_get_processed_count(thread_pool_t *pool);

#endif /* THREAD_POOL_H */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "thread_pool.h"
#include "logger.h"
#include "metrics.h"

static void* thread_worker(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (pool->is_running) {
        pthread_mutex_lock(&pool->queue_lock);

        while (pool->queue_head == NULL && pool->is_running) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_lock);
        }

        if (!pool->is_running) {
            pthread_mutex_unlock(&pool->queue_lock);
            break;
        }

        work_item_t *item = pool->queue_head;
        pool->queue_head = item->next;
        if (pool->queue_head == NULL) {
            pool->queue_tail = NULL;
        }
        pool->queue_size--;

        pthread_mutex_unlock(&pool->queue_lock);

        /* Process the packet */
        if (item->packet != NULL) {
            packet_parse(item->packet);
            packet_print(item->packet);
            pool->packets_processed++;
            
            /* Only record metrics during measurement phase (after warmup) */
            if (metrics_is_active()) {
                /* Record EtherType metrics (with safe NULL check) */
                if (item->packet->ethernet != NULL) {
                    /* EtherType is stored in network byte order, convert to host */
                    uint16_t ethertype = ntohs(item->packet->ethernet->ethertype);
                    metrics_record_ethertype(ethertype);
                    
                    /* Record L4 protocol metrics */
                    if (ethertype == 0x0800 && item->packet->ipv4 != NULL) {
                        /* IPv4 packet - record IP protocol */
                        metrics_record_protocol(item->packet->ipv4->protocol);
                    } else if (ethertype == 0x86DD && item->packet->raw_data != NULL && 
                               item->packet->packet_length >= 14 + 40) {
                        /* IPv6 packet - next header is at offset 14 + 6 = 20 */
                        uint8_t next_header = item->packet->raw_data[14 + 6];
                        metrics_record_protocol(next_header);
                    }
                }
                
                /* Calculate and record end-to-end latency */
                uint64_t now_ns = metrics_now_ns();
                uint64_t latency_ns = now_ns - item->packet->capture_ts_ns;
                metrics_observe_latency(latency_ns);
                
                /* Record processed packet metrics */
                metrics_inc_processed(item->packet->packet_length);
            }
            
            logger_debug("Processed packet (Total: %d)", pool->packets_processed);
        }

        free(item);
    }

    return NULL;
}

thread_pool_t* thread_pool_create(int num_threads, int max_queue_size) {
    if (num_threads <= 0 || max_queue_size <= 0) {
        logger_error("Invalid thread pool parameters");
        return NULL;
    }

    thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    if (pool == NULL) {
        logger_error("Failed to allocate memory for thread pool");
        return NULL;
    }

    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
    if (pool->threads == NULL) {
        logger_error("Failed to allocate memory for thread array");
        free(pool);
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_size = 0;
    pool->max_queue_size = max_queue_size;
    pool->is_running = 1;
    pool->packets_processed = 0;

    pthread_mutex_init(&pool->queue_lock, NULL);
    pthread_cond_init(&pool->queue_cond, NULL);

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, pool) != 0) {
            logger_error("Failed to create thread %d", i);
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    logger_info("Thread pool created with %d threads (max queue: %d)", num_threads, max_queue_size);
    return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (pool == NULL) return;

    pthread_mutex_lock(&pool->queue_lock);
    pool->is_running = 0;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_lock);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    /* Cleanup remaining items in queue */
    work_item_t *current = pool->queue_head;
    while (current != NULL) {
        work_item_t *next = current->next;
        packet_free(current->packet);
        free(current);
        current = next;
    }

    pthread_mutex_destroy(&pool->queue_lock);
    pthread_cond_destroy(&pool->queue_cond);

    free(pool->threads);
    free(pool);

    logger_info("Thread pool destroyed");
}

int thread_pool_enqueue(thread_pool_t *pool, packet_t *packet) {
    if (pool == NULL || packet == NULL) {
        logger_error("Invalid thread pool or packet");
        return -1;
    }

    work_item_t *item = (work_item_t *)malloc(sizeof(work_item_t));
    if (item == NULL) {
        logger_error("Failed to allocate memory for work item");
        return -1;
    }

    item->packet = packet;
    item->next = NULL;

    pthread_mutex_lock(&pool->queue_lock);

    if (pool->queue_size >= pool->max_queue_size) {
        logger_warn("Work queue is full (%d items)", pool->queue_size);
        pthread_mutex_unlock(&pool->queue_lock);
        free(item);
        metrics_inc_queue_drops();  /* Track queue drop */
        return -1;
    }

    if (pool->queue_tail == NULL) {
        pool->queue_head = item;
    } else {
        pool->queue_tail->next = item;
    }
    pool->queue_tail = item;
    pool->queue_size++;
    
    /* Update queue depth maximum watermark */
    metrics_update_queue_depth_max((uint32_t)pool->queue_size);

    pthread_cond_signal(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_lock);

    return 0;
}

int thread_pool_is_running(thread_pool_t *pool) {
    if (pool == NULL) return 0;
    
    pthread_mutex_lock(&pool->queue_lock);
    int running = pool->is_running;
    pthread_mutex_unlock(&pool->queue_lock);
    
    return running;
}

int thread_pool_get_processed_count(thread_pool_t *pool) {
    if (pool == NULL) return 0;
    
    pthread_mutex_lock(&pool->queue_lock);
    int count = pool->packets_processed;
    pthread_mutex_unlock(&pool->queue_lock);
    
    return count;
}

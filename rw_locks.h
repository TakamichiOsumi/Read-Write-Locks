#ifndef __RW_LOCKS__
#define __RW_LOCKS__

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Recursive reader threads manager required to check
 * invalid unlocking.
 */
typedef struct rec_rdt_manager {
    int thread_total_no;
    int insert_index;
    /*
     * Remember how many times the reader thread gets the locks.
     * For the first (non-recursive) lock, set one to each thread count.
     */
    int *reader_threads_count_in_CS;
    pthread_t *reader_thread_ids;
} rec_rdt_manager;

typedef struct rw_lock {
    uint16_t running_threads_in_CS;
    uint16_t waiting_reader_threads;
    uint16_t writer_recursive_count;
    uint16_t waiting_writer_threads;
    bool is_locked_by_reader;
    bool is_locked_by_writer;
    pthread_t writer_thread_in_CS;
    rec_rdt_manager manager;
    pthread_cond_t state_cv;
    pthread_mutex_t state_mutex;
} rw_lock;

rw_lock *rw_lock_init(unsigned int thread_total_no);
void rw_lock_rd_lock(rw_lock *rwl);
void rw_lock_wr_lock(rw_lock *rwl);
void rw_lock_unlock(rw_lock *rwl);
void rw_lock_destroy(rw_lock *rwl);

#endif

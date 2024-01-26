#ifndef __RW_LOCKS__
#define __RW_LOCKS__

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct rw_lock {
    uint16_t running_threads_in_CS;
    uint16_t waiting_reader_threads;
    uint16_t waiting_writer_threads;
    bool is_locked_by_reader;
    bool is_locked_by_writer;
    pthread_t writer_thread_in_CS;
    pthread_cond_t state_cv;
    pthread_mutex_t state_mutex;
} rw_lock;

rw_lock *rw_lock_init(void);
void rw_lock_rd_lock(rw_lock *rwl);
void rw_lock_wr_lock(rw_lock *rwl);
void rw_lock_unlock(rw_lock *rwl);
void rw_lock_destroy(rw_lock *rwl);

#endif

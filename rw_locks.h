#ifndef __THREAD_MONITORS__
#define __THREAD_MONITORS__

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_MAX_READER_THREADS_NUM 0xFFFF
#define DEFAULT_MAX_WRITER_THREADS_NUM 1

/* Recursive Read/Write count manager */
typedef struct rec_count_manager {
    uint16_t max_threads_count_in_CS;
    uint16_t insert_index;
    uint16_t *threads_count_in_CS;
    pthread_t *thread_ids;
} rec_count_manager;

typedef struct rw_lock {
    uint16_t running_threads_in_CS;

    bool reader_thread_in_CS;
    uint16_t running_reader_threads_in_CS;
    bool block_new_reader_thread_entry;
    uint16_t waiting_reader_threads;
    bool is_locked_by_reader;
    rec_count_manager reader_count_manager;

    bool writer_thread_in_CS;
    uint16_t running_writer_threads_in_CS;
    bool block_new_writer_thread_entry;
    uint16_t waiting_writer_threads;
    bool is_locked_by_writer;
    rec_count_manager writer_count_manager;

    /* Selectively sending a signal for biasedness property */
    pthread_cond_t reader_cv;
    pthread_cond_t writer_cv;

    pthread_mutex_t state_mutex;
} rw_lock;

typedef rw_lock monitor;

void my_assert(char *description, char *filename,
	       int lineno, int expr);

monitor *
create_monitor(unsigned int reader_thread_total_no,
	       unsigned int writer_thread_total_no);

#define rw_lock_init(reader_thread_total_no) \
    create_monitor(reader_thread_total_no, 1)

void rw_lock_rd_lock(rw_lock *rwl);
void rw_lock_wr_lock(rw_lock *rwl);
void rw_lock_unlock(rw_lock *rwl);
void rw_lock_destroy(rw_lock *rwl);

#endif

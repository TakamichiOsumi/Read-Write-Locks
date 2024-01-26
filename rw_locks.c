#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "rw_locks.h"

rw_lock *
rw_lock_init(void){
    rw_lock *new_rwl;

    if ((new_rwl = (rw_lock *)malloc(sizeof(rw_lock))) == NULL){
	perror("malloc");
	exit(-1);
    }

    pthread_mutex_init(&new_rwl->state_mutex, NULL);
    pthread_cond_init(&new_rwl->state_cv, NULL);
    new_rwl->running_threads_in_CS = 0;
    new_rwl->waiting_reader_threads = 0;
    new_rwl->waiting_writer_threads = 0;
    new_rwl->is_locked_by_reader = false;
    new_rwl->is_locked_by_writer = false;
    new_rwl->writer_thread_in_CS = NULL;

    return new_rwl;
}

void
rw_lock_rd_lock(rw_lock *rwl){
    pthread_mutex_lock(&rwl->state_mutex);
    /*
     * For any read operation, wait only if the lock is
     * taken by a write thread.
     */
    while(rwl->writer_thread_in_CS && rwl->is_locked_by_writer){
	rwl->waiting_reader_threads++;
	pthread_cond_wait(&rwl->state_cv,
			  &rwl->state_mutex);
	rwl->waiting_reader_threads++;
    }
    assert(rwl->writer_thread_in_CS == NULL);
    assert(rwl->is_locked_by_writer == false);

    rwl->running_threads_in_CS++;
    rwl->is_locked_by_reader = true;

    pthread_mutex_unlock(&rwl->state_mutex);
}

void
rw_lock_wr_lock(rw_lock *rwl){
    pthread_mutex_lock(&rwl->state_mutex);
    /*
     * For any write operation, wait if the lock is
     * taken by a writer thread or if any reader thread
     * has gained the lock already.
     */
    while((rwl->writer_thread_in_CS && rwl->is_locked_by_writer) ||
	  (rwl->is_locked_by_reader && rwl->running_threads_in_CS > 0)){
	rwl->waiting_writer_threads++;
	pthread_cond_wait(&rwl->state_cv,
			  &rwl->state_mutex);
	rwl->waiting_writer_threads++;
    }
    assert(rwl->writer_thread_in_CS == NULL);
    assert(rwl->is_locked_by_reader == false);
    assert(rwl->is_locked_by_writer == false);
    assert(rwl->running_threads_in_CS == 0);

    rwl->running_threads_in_CS = 1;
    rwl->is_locked_by_writer = true;
    rwl->writer_thread_in_CS = pthread_self();

    pthread_mutex_unlock(&rwl->state_mutex);
}

void
rw_lock_unlock(rw_lock *rwl){
    pthread_mutex_lock(&rwl->state_mutex);
    if (rwl->is_locked_by_writer){
	assert(rwl->writer_thread_in_CS == pthread_self());
	assert(rwl->running_threads_in_CS == 1);
	assert(rwl->is_locked_by_reader == false);

	rwl->running_threads_in_CS = 0;
	rwl->is_locked_by_writer = false;
	rwl->writer_thread_in_CS = NULL;
	pthread_cond_signal(&rwl->state_cv);
    }else if (rwl->is_locked_by_reader){
	assert(rwl->writer_thread_in_CS == NULL);
	assert(rwl->is_locked_by_writer == false);

	rwl->running_threads_in_CS--;
	if (rwl->running_threads_in_CS == 0){
	    rwl->is_locked_by_reader = false;
	}
	pthread_cond_signal(&rwl->state_cv);
    }else{
	/*
	 * The application program has called rw_lock_unlock()
	 * even when no one is taking the lock. Raise the assertion failure.
	 */
	assert(0);
    }
    pthread_mutex_unlock(&rwl->state_mutex);
}

void rw_lock_destroy(rw_lock *rwl){
    assert(rwl->running_threads_in_CS == 0);
    assert(rwl->waiting_reader_threads == 0);
    assert(rwl->waiting_writer_threads == 0);
    assert(rwl->is_locked_by_reader == false);
    assert(rwl->is_locked_by_writer == false);
    assert(rwl->writer_thread_in_CS == NULL);
    pthread_cond_destroy(&rwl->state_cv);
    pthread_mutex_destroy(&rwl->state_mutex);
}

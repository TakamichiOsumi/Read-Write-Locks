#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rw_locks.h"

/* Turn on the self debug assertion for more advanced tests */
#define DEBUG_RW_LOCK

/*
 * Raise the SIGUSR1 signal to notify the application bug.
 *
 * Exported so as to be utilized by the application side of this rw_lock library.
 */
void
my_assert(char *description, char *filename, int lineno, int expr){
#ifdef DEBUG_RW_LOCK
    /* Raise the assertion failure if the 'expr' is equal to zero */
    if (expr == 0){
	if (description != NULL){
	    fprintf(stderr, "%s:%d:%s\n", filename, lineno, description);
	}else{
	    fprintf(stderr, "%s:%d\n", filename, lineno);
	}
	raise(SIGUSR1);
    }
#else
    assert(expr);
#endif
}

/*
 * Find the self index from the reader thread manager.
 *
 * On failure, return -1.
 */
static int
rw_lock_get_reader_index(rw_lock *rwl){
    rec_rdt_manager *manager = &rwl->manager;
    int index;

    for (index = 0; index < manager->thread_total_no; index++){
	if (manager->reader_thread_ids[index] == pthread_self()){
	    return index;
	}
    }

    return -1;
}

rw_lock *
rw_lock_init(unsigned int thread_total_no){
    rw_lock *new_rwl;
    int i;

    my_assert(NULL, __FILE__, __LINE__, thread_total_no >= 0);

    if ((new_rwl = (rw_lock *) malloc(sizeof(rw_lock))) == NULL){
	perror("malloc");
	exit(-1);
    }

    if (pthread_mutex_init(&new_rwl->state_mutex, NULL) != 0){
	perror("pthread_mutex_init");
	exit(-1);
    }

    if (pthread_cond_init(&new_rwl->state_cv, NULL) != 0){
	perror("pthread_cond_init");
	exit(-1);
    }

    /* Reader thread manager */
    new_rwl->manager.thread_total_no = thread_total_no;
    if ((new_rwl->manager.reader_threads_count_in_CS =
	 (int *) malloc(sizeof(int) * thread_total_no)) == NULL){
	perror("malloc");
	exit(-1);
    }

    if ((new_rwl->manager.reader_thread_ids =
	 (pthread_t *) malloc(sizeof(pthread_t) * thread_total_no)) == NULL){
	perror("malloc");
	exit(-1);
    }

    new_rwl->manager.insert_index = 0;

    for (i = 0; i < thread_total_no; i++){
	new_rwl->manager.reader_threads_count_in_CS[i] = 0;
	new_rwl->manager.reader_thread_ids[i] = NULL;
    }

    new_rwl->running_threads_in_CS = 0;
    new_rwl->waiting_reader_threads = 0;
    new_rwl->writer_recursive_count = 0;
    new_rwl->waiting_writer_threads = 0;
    new_rwl->is_locked_by_reader = false;
    new_rwl->is_locked_by_writer = false;
    new_rwl->writer_thread_in_CS = NULL;

    return new_rwl;
}

void
rw_lock_rd_lock(rw_lock *rwl){
    rec_rdt_manager *manager;
    int index;

    pthread_mutex_lock(&rwl->state_mutex);

    /*
     * For any read operation, wait only if the lock is taken
     * by a write thread.
     *
     * There is no need to check the reader related conditions
     * in the below predicate, because even if other reader
     * thread took a lock, it is harmless to set the flag of
     * reader's lock true again (and also, to increment the
     * number of reader threads).
     */
    while(rwl->writer_thread_in_CS && rwl->is_locked_by_writer){
	rwl->waiting_reader_threads++;
	pthread_cond_wait(&rwl->state_cv, &rwl->state_mutex);
	rwl->waiting_reader_threads++;
    }

    my_assert(NULL, __FILE__, __LINE__,
	      rwl->writer_thread_in_CS == NULL);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_writer == false);

    /*
     * Manage reader thread's count of the lock, including recursive ones
     */
    manager = &rwl->manager;
    if ((index = rw_lock_get_reader_index(rwl)) == -1){
	index = manager->insert_index;
	manager->insert_index++;
    }

    /*
     * If this is a recursive lock, then increment the count
     */
    if (rwl->is_locked_by_reader &&
	manager->reader_thread_ids[index] != NULL &&
	manager->reader_threads_count_in_CS[index] != 0){
	manager->reader_threads_count_in_CS[index]++;
	printf("[%s] %p sets threads_count_in_CS[%d] = '%d' by recursive lock\n",
	       __FUNCTION__, pthread_self(), index,
	       manager->reader_threads_count_in_CS[index]);
    }else{
	/* Ensure this lock is a completely new lock */
	my_assert(NULL, __FILE__, __LINE__,
		  manager->reader_threads_count_in_CS[index] == 0);

	rwl->running_threads_in_CS++;
	rwl->is_locked_by_reader = true;
	manager->reader_threads_count_in_CS[index] = 1;
	manager->reader_thread_ids[index] = pthread_self();

	printf("[%s] %p created threads_count_in_CS[%d] = '%d' by a new lock\n",
	       __FUNCTION__, pthread_self(), index,
	       manager->reader_threads_count_in_CS[index]);
    }

    pthread_mutex_unlock(&rwl->state_mutex);
}

void
rw_lock_wr_lock(rw_lock *rwl){
    pthread_mutex_lock(&rwl->state_mutex);

    /* Support the recursive locking */
    if (rwl->is_locked_by_writer && rwl->writer_thread_in_CS == pthread_self()){
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_threads_in_CS == 1);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->is_locked_by_reader == false);

	rwl->writer_recursive_count++;
	printf("[%s] %p got a recursive lock (count = %d)\n",
	       __FUNCTION__, pthread_self(), rwl->writer_recursive_count);
	pthread_mutex_unlock(&rwl->state_mutex);
	return;
    }

    /*
     * For any new write operation, wait if the lock is
     * taken by any other writer thread or if any reader thread
     * is taking the lock.
     */
    while((rwl->writer_thread_in_CS && rwl->is_locked_by_writer) ||
	  (rwl->is_locked_by_reader && rwl->running_threads_in_CS > 0)){
	rwl->waiting_writer_threads++;
	pthread_cond_wait(&rwl->state_cv, &rwl->state_mutex);
	rwl->waiting_writer_threads++;
    }

    my_assert(NULL, __FILE__, __LINE__,
	      rwl->writer_thread_in_CS == NULL);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_reader == false);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_writer == false);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->running_threads_in_CS == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->writer_recursive_count == 0);

    rwl->writer_recursive_count = 1;
    rwl->running_threads_in_CS = 1;
    rwl->is_locked_by_writer = true;
    rwl->writer_thread_in_CS = pthread_self();

    pthread_mutex_unlock(&rwl->state_mutex);
}

void
rw_lock_unlock(rw_lock *rwl){
    pthread_mutex_lock(&rwl->state_mutex);

    if (rwl->is_locked_by_writer){
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->writer_thread_in_CS == pthread_self());
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_threads_in_CS == 1);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->is_locked_by_reader == false);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->writer_recursive_count > 0);

	/*
	 * When there were any calls of recursive lock, then
	 * decrement the recursive count for writer thread and
	 * keep holding the lock.
	 */
	if (rwl->writer_recursive_count > 0){
	    rwl->writer_recursive_count--;

	    printf("[%s] %p released a recursive lock (recursive count = %d)\n",
	       __FUNCTION__, pthread_self(), rwl->writer_recursive_count);

	    /* This writer thread is done with recursive lock work */
	    if (rwl->writer_recursive_count == 0){
		rwl->running_threads_in_CS = 0;
		rwl->is_locked_by_writer = false;
		rwl->writer_thread_in_CS = NULL;
		/* Send a signal only if there is any waiting threads */
		if (rwl->waiting_reader_threads > 0 ||
		    rwl->waiting_writer_threads > 0)
		    pthread_cond_signal(&rwl->state_cv);
	    }
	}
    }else if (rwl->is_locked_by_reader){
	rec_rdt_manager *manager = &rwl->manager;
	int index;

	my_assert(NULL, __FILE__, __LINE__,
		  rwl->writer_thread_in_CS == NULL);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->is_locked_by_writer == false);

	/*
	 * Failure to find the entry of reader thread index
	 * means that the C.S. is locked by some reader threads,
	 * but there was no corresponding call of rw_lock_rd_lock()
	 * for this thread.
	 *
	 * This is the invalid unlock where one thread tries to
	 * unlock even when it didn't get any lock.
	 *
	 * Raise an assertion failure.
	 */
	if ((index = rw_lock_get_reader_index(rwl)) == -1)
	    my_assert(NULL, __FILE__, __LINE__, 0);

	if (manager->reader_threads_count_in_CS[index] - 1 > 1){
	    /* This thread utilizes the recursive unlock. Decrement the count */
	    manager->reader_threads_count_in_CS[index]--;
	}else{
	    /* This thread is done with its work in the C.S. section */
	    rwl->running_threads_in_CS--;
	    manager->reader_threads_count_in_CS[index] = 0;
	    printf("[%s] %p has released all its reader locks\n",
		   __FUNCTION__, pthread_self());

	    if (rwl->running_threads_in_CS == 0){
		rwl->is_locked_by_reader = false;

		/* Send a signal only if there is any waiting threads */
		if (rwl->waiting_reader_threads > 0 ||
		    rwl->waiting_writer_threads > 0)
		    pthread_cond_signal(&rwl->state_cv);
	    }
	}
    }else{
	/*
	 * The application program has called rw_lock_unlock()
	 * even when no one is taking the lock. This path means
	 * there was no corresponding call of either rw_lock_rd_lock()
	 * or rw_lock_wr_lock().
	 *
	 * Raise the assertion failure.
	 */
	my_assert(NULL, __FILE__, __LINE__, 0);
    }
    pthread_mutex_unlock(&rwl->state_mutex);
}

void
rw_lock_destroy(rw_lock *rwl){
    int i;

    my_assert(NULL, __FILE__, __LINE__,
	      rwl->running_threads_in_CS == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->waiting_reader_threads == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->waiting_writer_threads == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->writer_recursive_count == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_reader == false);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_writer == false);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->writer_thread_in_CS == NULL);
    for (i = 0; i < rwl->manager.thread_total_no; i++){
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->manager.reader_threads_count_in_CS[i] == 0);
    }

    pthread_cond_destroy(&rwl->state_cv);
    pthread_mutex_destroy(&rwl->state_mutex);
}

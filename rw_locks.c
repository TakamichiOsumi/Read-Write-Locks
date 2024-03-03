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

void *
my_malloc(size_t size){
    void *p;

    if ((p = malloc(size)) == NULL){
	perror("malloc");
	exit(-1);
    }

    return p;
}

/*
 * Find the self index from the reader thread manager.
 *
 * On failure, return -1.
 */
static int
rw_lock_get_manager_index(rec_count_manager *manager){
    int index;

    printf("Searching for thread = %p within 'max_threads_count' range: %d\n",
	   pthread_self(), manager->max_threads_count_in_CS);
    for (index = 0; index < manager->max_threads_count_in_CS; index++){
	if (manager->thread_ids[index] == pthread_self()){
	    return index;
	}
    }

    return -1;
}

static void
rw_lock_init_manager(rec_count_manager *manager,
		     unsigned int upper_limit){
    int i = 0;

    manager->max_threads_count_in_CS = upper_limit;
    manager->insert_index = 0;
    manager->threads_count_in_CS =
	(uint16_t *) my_malloc(sizeof(int) * upper_limit);
    manager->thread_ids =
	(pthread_t *) my_malloc(sizeof(pthread_t) * upper_limit);
    for(; i < upper_limit; i++){
	manager->threads_count_in_CS[i] = 0;
	manager->thread_ids[i] = NULL;
    }
}

monitor *
create_monitor(unsigned int reader_thread_total_no,
	       unsigned int writer_thread_total_no){
    rw_lock *new_rwl;

    my_assert(NULL, __FILE__, __LINE__,
	      reader_thread_total_no >= 0);
    my_assert(NULL, __FILE__, __LINE__,
	      writer_thread_total_no >= 0);

    new_rwl = (rw_lock *) my_malloc(sizeof(rw_lock));

    if (pthread_cond_init(&new_rwl->writer_cv, NULL) != 0){
	perror("pthread_cond_init");
	exit(-1);
    }

    /* reader */
    new_rwl->running_reader_threads_in_CS = 0;
    new_rwl->block_new_reader_thread_entry = false;
    new_rwl->waiting_reader_threads = 0;
    new_rwl->is_locked_by_reader = false;
    rw_lock_init_manager(&new_rwl->reader_count_manager,
			 reader_thread_total_no);

    /* writer */
    new_rwl->running_writer_threads_in_CS = 0;
    new_rwl->block_new_writer_thread_entry = false;
    new_rwl->waiting_writer_threads = 0;
    new_rwl->is_locked_by_writer = false;
    rw_lock_init_manager(&new_rwl->writer_count_manager,
			 writer_thread_total_no);

    if (pthread_mutex_init(&new_rwl->state_mutex, NULL) != 0){
	perror("pthread_mutex_init");
	exit(-1);
    }

    if (pthread_cond_init(&new_rwl->reader_cv, NULL) != 0){
	perror("pthread_cond_init");
	exit(-1);
    }

    if (pthread_mutex_init(&new_rwl->state_mutex, NULL) != 0){
	perror("pthread_mutex_init");
	exit(-1);
    }

    return new_rwl;
}

void
read_lock_monitor(monitor *mntr){
    rec_count_manager *mngr;
    int index;

    pthread_mutex_lock(&mntr->state_mutex);

    mngr = &mntr->reader_count_manager;

    /*
     * Wait if
     * (1) there is at least one writer thread in the C.S.
     * (2) the current reader thread joins the C.S,
     *     then the number of accepted reader threaas will be exceeded.
     * (3) the biasedness property is on for writer thread
     *     and new entry of reader thread is blocked.
     */
    while((mntr->is_locked_by_writer == true) ||
	  (mngr->max_threads_count_in_CS < mntr->running_reader_threads_in_CS + 1) ||
	  (mntr->block_new_reader_thread_entry == true)){
	mntr->waiting_reader_threads++;
	pthread_cond_wait(&mntr->reader_cv, &mntr->state_mutex);
	mntr->waiting_reader_threads--;
    }

    /*
     * Manage reader thread's count of the lock, including recursive ones
     */
    if ((index = rw_lock_get_manager_index(mngr)) == -1){
	index = mngr->insert_index;
	mngr->insert_index++;
	my_assert("Overflow of the reader count index",
		  __FILE__, __LINE__, index < mngr->max_threads_count_in_CS);
    }

    /*
     * If this lock is already taken by a reader thread, then check if
     * it is a recursive lock. If so, increment the count for recursiveness.
     */
    if (mntr->is_locked_by_reader &&
	mngr->thread_ids[index] != NULL &&
	mngr->threads_count_in_CS[index] != 0){

	mngr->threads_count_in_CS[index]++;
	printf("[%s] %p sets threads_count_in_CS[%d] = '%d' by recursive lock\n",
	       __FUNCTION__, pthread_self(), index,
	       mngr->threads_count_in_CS[index]);
    }else{
	/* Ensure the lock taken by this thread is a completely new one */
	my_assert(NULL, __FILE__, __LINE__,
		  mngr->threads_count_in_CS[index] == 0);

	/*
	 * This is not a recursive lock, which means a new thread
	 * has joined the C.S.
	 *
	 * Increment the reader threads count for the C.S.
	 */
	mntr->running_reader_threads_in_CS++;
	mntr->is_locked_by_reader = true;
	mngr->threads_count_in_CS[index] = 1;
	mngr->thread_ids[index] = pthread_self();

	printf("[%s] %p created threads_count_in_CS[%d] = '%d' by a new lock\n",
	       __FUNCTION__, pthread_self(), index, mngr->threads_count_in_CS[index]);
    }

    pthread_mutex_unlock(&mntr->state_mutex);
}

void
write_lock_monitor(monitor *mntr){
    rec_count_manager *mngr;
    int index;

    pthread_mutex_lock(&mntr->state_mutex);

    mngr = &mntr->writer_count_manager;

    /*
     * See the details about the logics in read_lock_monitor.
     * The same applies here too.
     */
    while((mntr->is_locked_by_reader == true) ||
	  (mngr->max_threads_count_in_CS < mntr->running_writer_threads_in_CS + 1) ||
	  (mntr->block_new_writer_thread_entry == true)){
	mntr->waiting_writer_threads++;
	pthread_cond_wait(&mntr->writer_cv, &mntr->state_mutex);
	mntr->waiting_writer_threads--;
    }

    /*
     * Manage writer thread's count of the lock, including recursive ones
     */
    if ((index = rw_lock_get_manager_index(mngr)) == -1){
	index = mngr->insert_index;
	mngr->insert_index++;
	my_assert("Overflow of the writer count index",
		  __FILE__, __LINE__, index < mngr->max_threads_count_in_CS);
    }

    /*
     * If this is a recursive lock, then increment the recursive count.
     */
    if (mntr->is_locked_by_writer &&
	mngr->thread_ids[index] != NULL &&
	mngr->threads_count_in_CS[index] != 0){
	mngr->threads_count_in_CS[index]++;
	printf("[%s] %p sets threads_count_in_CS[%d] = '%d' by recursive lock\n",
	       __FUNCTION__, pthread_self(), index,
	       mngr->threads_count_in_CS[index]);
    }else{
	/* Ensure this lock is a completely new lock */
	my_assert(NULL, __FILE__, __LINE__,
		  mngr->threads_count_in_CS[index] == 0);

	mntr->running_writer_threads_in_CS++;
	mntr->is_locked_by_writer = true;
	mngr->threads_count_in_CS[index] = 1;
	mngr->thread_ids[index] = pthread_self();

	printf("[%s] %p created threads_count_in_CS[%d] = '%d' by a new lock\n",
	       __FUNCTION__, pthread_self(), index, mngr->threads_count_in_CS[index]);
    }

    pthread_mutex_unlock(&mntr->state_mutex);
}

void
unlock_monitor(monitor *rwl){
    rec_count_manager *mngr;
    int index;

    pthread_mutex_lock(&rwl->state_mutex);

    if (rwl->is_locked_by_writer){
	/* Writer path */

	/* Must not be locked by any reader threads */
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_reader_threads_in_CS == 0);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->is_locked_by_reader == false);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_writer_threads_in_CS > 0);

	/* Search the recursive count of this writer thread */
	mngr = &rwl->writer_count_manager;
	if ((index = rw_lock_get_manager_index(mngr)) == -1)
	    my_assert(NULL, __FILE__, __LINE__, 0);

	mngr->threads_count_in_CS[index]--;

	if (mngr->threads_count_in_CS[index] == 0){
	    /* This writer thread is done with recursive lock work */
	    rwl->running_writer_threads_in_CS--;

	    if (rwl->running_writer_threads_in_CS == 0){

		rwl->is_locked_by_writer = false;

		/* Send a signal only if there is any waiting threads */
		if (rwl->waiting_reader_threads > 0){
		    pthread_cond_broadcast(&rwl->reader_cv);
		}
		if (rwl->waiting_writer_threads > 0){
		    pthread_cond_broadcast(&rwl->writer_cv);
		}

		/*
		 * This is the last writer thread in the C.S. at this moment.
		 * Wake up waiting reader threads if any preferentially.
		 */
		/*
		if (rwl->waiting_reader_threads > 0){
		    pthread_cond_broadcast(&rwl->reader_cv);
		    rwl->block_new_writer_thread_entry = true;
		    rwl->block_new_reader_thread_entry = false;
		}else if (rwl->waiting_writer_threads > 0){
		    pthread_cond_broadcast(&rwl->writer_cv);
		    }*/
	    }else{
		/* Replacement property */
	    }
	}
    }else if (rwl->is_locked_by_reader){
	/* Reader path */
	mngr = &rwl->reader_count_manager;

	my_assert(NULL, __FILE__, __LINE__,
		  rwl->is_locked_by_writer == false);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_writer_threads_in_CS == 0);
	my_assert(NULL, __FILE__, __LINE__,
		  rwl->running_reader_threads_in_CS > 0);

	if ((index = rw_lock_get_manager_index(mngr)) == -1)
	    my_assert(NULL, __FILE__, __LINE__, 0);

	mngr->threads_count_in_CS[index]--;

	if (mngr->threads_count_in_CS[index] == 0){
	    /* This reader thread is done with its work in the C.S. section */
	    rwl->running_reader_threads_in_CS--;

	    if (rwl->running_reader_threads_in_CS == 0){
		rwl->is_locked_by_reader = false;

		/* Send a signal only if there is any waiting threads */
		if (rwl->waiting_reader_threads > 0){
		    pthread_cond_broadcast(&rwl->reader_cv);
		}
		if (rwl->waiting_writer_threads > 0){
		    pthread_cond_broadcast(&rwl->writer_cv);
		}

		/*
		 * This is the last reader thread in the C.S. at this moment.
		 *
		 * Wake up waiting writer threads if any preferentially.
		 */
		/*
		if (rwl->waiting_writer_threads > 0){
		    pthread_cond_broadcast(&rwl->writer_cv);
		    rwl->block_new_writer_thread_entry = false;
		    rwl->block_new_reader_thread_entry = true;
		}else if (rwl->waiting_reader_threads > 0){
		    pthread_cond_broadcast(&rwl->reader_cv);
		}*/
	    }else{
		/* Replacement property */
		/* pthread_cond_signal(&rwl->writer_cv); */
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

static void
verify_manager_with_all_zeros(rec_count_manager *manager){
    int i;

    for (i = 0; i < manager->max_threads_count_in_CS; i++){
	my_assert(NULL, __FILE__, __LINE__,
		  manager->threads_count_in_CS[i] == 0);
    }

}

void
destroy_monitor(monitor *rwl){
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->waiting_reader_threads == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->waiting_writer_threads == 0);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_reader == false);
    my_assert(NULL, __FILE__, __LINE__,
	      rwl->is_locked_by_writer == false);
    verify_manager_with_all_zeros(&rwl->reader_count_manager);
    verify_manager_with_all_zeros(&rwl->writer_count_manager);
    pthread_cond_destroy(&rwl->reader_cv);
    pthread_cond_destroy(&rwl->writer_cv);
    pthread_mutex_destroy(&rwl->state_mutex);
}

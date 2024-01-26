#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "rw_locks.h"

/*
 * All threads need to register signal handler 'assert_dump_handler'
 * for debugging via 'prepare_assertion_failure'.
 */
void
assert_dump_handler(int sig, siginfo_t *info, void *q){
    fprintf(stderr, "\n!!! the thread id where raised assertion failure : %p\n\n",
	    pthread_self());
    exit(-1);
}

void
prepare_assertion_failure(void){
    struct sigaction act;

    act.sa_sigaction = assert_dump_handler;
    act.sa_flags = SIGABRT;
    sigemptyset(&act.sa_mask);
    sigaction(SIGABRT, &act, NULL);
}

typedef struct thread_unique {
    /* Data hold by each thread */
    int thread_id;
    /* Data shared among all the threads */
    rw_lock *rwl;
} thread_unique;

static void *
write_thread_cb(void *arg){
    thread_unique *unique = (thread_unique *) arg;
    int i;

    for (i = 0; i < 10; i++){
	printf("[%s] (id = %d & pthread_id = %p) will get the rw-lock\n",
	       __FUNCTION__, unique->thread_id, pthread_self());
	rw_lock_wr_lock(unique->rwl);
	/*
	 * No need to implement an actual write operation for debug.
	 * Do nothing here. An assertion check below is sufficient.
	 */
	printf("[%s] (id = %d & pthread_id = %p) has entered C.S. with %d thread\n",
	       __FUNCTION__, unique->thread_id, pthread_self(),
	       unique->rwl->running_threads_in_CS);
	/*
	 * Cause an assertion failure if more than one thread has
	 * entered the C.S. during the write operation.
	 */
	assert(unique->rwl->running_threads_in_CS == 1);

	rw_lock_unlock(unique->rwl);
	printf("[%s] (id = %d & pthread_id = %p) has left C.S. with %d thread\n",
	       __FUNCTION__, unique->thread_id, pthread_self(),
	       unique->rwl->running_threads_in_CS);
    }

    free(arg);
    return NULL;
}

static void *
read_thread_cb(void *arg){
    thread_unique *unique = (thread_unique *) arg;
    int i;

    for (i = 0; i < 10; i++){
	printf("[%s] (id = %d & pthread_id = %p) will get the rw-lock\n",
	       __FUNCTION__, unique->thread_id, pthread_self());
	rw_lock_rd_lock(unique->rwl);
	/*
	 * No need to implement an actual read operation for debug.
	 * Do nothing here. See write_thread_cb also.
	 */
	printf("[%s] (id = %d & pthread_id = %p) has entered C.S. with %d threads\n",
	       __FUNCTION__, unique->thread_id, pthread_self(),
	       unique->rwl->running_threads_in_CS);
	/* Make sure there is more than one thread in the C.S. */
	assert(unique->rwl->running_threads_in_CS >= 1);
	rw_lock_unlock(unique->rwl);
	printf("[%s] (id = %d & pthread_id = %p) has left C.S. with %d threads\n",
	       __FUNCTION__, unique->thread_id, pthread_self(),
	       unique->rwl->running_threads_in_CS);
    }

    free(arg);
    return NULL;
}

static void
rw_threads_test(void){
#define THREADS_TOTAL_NO 16

    pthread_t handlers[THREADS_TOTAL_NO];
    rw_lock *rwl;
    int i;

    /* Set up the common setting and shared resource */
    prepare_assertion_failure();

    if ((rwl = malloc(sizeof(rw_lock))) == NULL){
	perror("malloc");
	exit(-1);
    }
    rwl = rw_lock_init();

    for (i = 0; i < THREADS_TOTAL_NO; i++){
	thread_unique *unique;

	if ((unique = malloc(sizeof(thread_unique))) == NULL){
	    perror("malloc");
	    exit(-1);
	}
	unique->thread_id = i;
	unique->rwl = rwl;

/* just for testing */
#define MANY_WRITER_THREADS (i % 4 <= 2)
#define MANY_READER_THREADS (i % 8 == 0)

	if (MANY_READER_THREADS){
	    if (pthread_create(&handlers[i], NULL,
			       write_thread_cb, (void *) unique) != 0){
		perror("pthread_create");
		exit(-1);
	    }
	}else{
	    if (pthread_create(&handlers[i], NULL,
			       read_thread_cb, (void *) unique) != 0){
		perror("pthread_create");
		exit(-1);
	    }
	}
    }
}

int
main(int argc, char **argv){

    printf("<Tests for thread rw-locks>\n");
    rw_threads_test();

    pthread_exit(0);

    return 0;
}

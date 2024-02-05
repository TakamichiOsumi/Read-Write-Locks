#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "rw_locks.h"

/* For debugging tests */
static sigjmp_buf env;
static bool expected_failure_raised;

/*
 * All execution units need to register signal handler 'assert_dump_handler'
 * for testing via 'prepare_assertion_failure'.
 */
void
assert_dump_handler(int sig, siginfo_t *info, void *q){
    char msg[] = "The signal handler for debug has been called !\n";

    write(STDOUT_FILENO, msg, sizeof(msg));

    expected_failure_raised = true;

    siglongjmp(env, 1);
}

void
prepare_assertion_failure(void){
    struct sigaction new_act;

    new_act.sa_sigaction = assert_dump_handler;
    new_act.sa_flags = SA_SIGINFO;
    sigemptyset(&new_act.sa_mask);

    if (sigaction(SIGUSR1, &new_act, NULL) != 0){
	perror("sigaction");
	exit(-1);
    }
}

static void
test_unlock_without_locking(){
    rw_lock *rwl = rw_lock_init(1);

    if (sigsetjmp(env, 1) == 0){
	/*
	 * <Scenario 1>
	 *
	 * rw_lock is forced to be unlocked without any lock operation.
	 */
	rw_lock_unlock(rwl);
    }else{
	if (!expected_failure_raised){
	    printf("NG : [%s] The expected assertion failure doesn't work\n",
		   __FUNCTION__);
	    exit(-1);
	}else{
	    printf("OK : [%s] The expected assertion failure works\n",
		   __FUNCTION__);
	    rw_lock_destroy(rwl);
	}
    }
}

static void
test_destory_rwl_with_lock(){
    rw_lock *rwl = rw_lock_init(1);

    if (sigsetjmp(env, 1) == 0){
	/*
	 * <Scenario 2>
	 *
	 * rw_lock is forced to be destroyed when a lock is taken and not released.
	 */
	rw_lock_rd_lock(rwl);
	rw_lock_destroy(rwl);
    }else{
	if (!expected_failure_raised){
	    printf("NG : [%s] The expected assertion failure doesn't work\n",
		   __FUNCTION__);
	    exit(-1);
	}else{
	    printf("OK : [%s] The expected assertion failure works\n",
		   __FUNCTION__);
	    rw_lock_unlock(rwl);
	    rw_lock_destroy(rwl);
	}
    }
}

/* <Scenario 3 > */
/*
 * Step1 : T1_flag and T2_flag gets updated to true by T1 and T2 after their read locks.
 * Step2 : T3_flag detects it and calls rw_lock_unlock without the call of rw_lock_rd_lock.
 * Step3 : Step2 raises the assertion failure.
 * Step4 : T3 resumes its execution from the failure and lets T1 release its read lock.
 * Step5 : T1 unlocks the read lock and lets T2 release its read lock.
 * Step6 : T2 unlocks the read lock and lets T3 cleans up all the resources.
 * Step7 : T3 destroys the read/write lock object.
 */
bool T1_flag = false, T2_flag = false, T3_flag = false;
bool T1_released_rdlock = false, T2_released_rdlock = false;

typedef struct thread_data {
    uintptr_t thread_id;
    rw_lock *rwl;
} thread_data;

static thread_data *
gen_thread_data(uintptr_t thread_id, rw_lock *rwl){
    thread_data *td;

    if ((td = (thread_data *) malloc(sizeof(thread_data))) == NULL){
	    perror("malloc");
	    exit(1);
    }

    td->thread_id = thread_id;
    td->rwl = rwl;

    return td;
}

static void *
lock_and_wait_cb(void *arg){
    thread_data *tdata = (thread_data *) arg;
    rw_lock *rwl = tdata->rwl;

    if (tdata->thread_id == 1){

	rw_lock_rd_lock(rwl);

	T1_flag = true;
	while(!T3_flag)
	    ;

	rw_lock_unlock(rwl);
	T1_released_rdlock = true;

	printf("[%s] T1 has set the flag = true\n", __FUNCTION__);

    }else if (tdata->thread_id == 2){

	rw_lock_rd_lock(rwl);
	T2_flag = true;

	while(!T1_released_rdlock)
	    ;
	rw_lock_unlock(rwl);
	T2_released_rdlock = true;

	printf("[%s] T3 has set the flag = true\n", __FUNCTION__);
    }

    return NULL;
}

static void *
wait_and_unlock_cb(void *arg){

    thread_data *tdata = (thread_data *) arg;
    rw_lock *rwl = tdata->rwl;

    if (sigsetjmp(env, 1) == 0){

	printf("[%s] T3 waits until other threads is done with read locks\n",
	       __FUNCTION__);

	while(!(T1_flag && T2_flag))
	    ;

	printf("[%s] T3 breaks the loop, Let it unlock a rdlock without locking\n",
	       __FUNCTION__);
	/* Will hit the assertion failure (after holding the mutex lock in rwl) */
	rw_lock_unlock(rwl);

    }else{
	if (!expected_failure_raised){
	    printf("NG : [%s] The expected assertion failure doesn't work\n",
		   __FUNCTION__);
	    exit(-1);
	}else{
	    printf("OK : [%s] The expected assertion failure works\n",
		   __FUNCTION__);

	    /* T3 raised the failure with holding the lock */
	    pthread_mutex_unlock(&rwl->state_mutex);

	    T3_flag = true;

	    while(!T2_released_rdlock)
		;

	    rw_lock_destroy(rwl);

	    printf("[%s] rw_lock object has been cleaned up correctly\n",
		   __FUNCTION__);
	}
    }
    return NULL;
}

static void
test_unregistered_thread_unlocking(){
#define THREADS_NUM 3

    rw_lock *rwl;
    pthread_t handlers[THREADS_NUM];
    thread_data *T1_data, *T2_data, *T3_data;

    /*
     * <Scenario 3>
     *
     * Prepare three thread T1, T2 and T3.
     * After T1 and T2 take the read locks, T3 tries to release a read lock.
     * T3 triggers the assertion failure for invalid unlocking.
     */
    rwl = rw_lock_init(THREADS_NUM);
    T1_data = gen_thread_data(1, rwl);
    T2_data = gen_thread_data(2, rwl);
    T3_data = gen_thread_data(3, rwl);
    pthread_create(&handlers[0], NULL, lock_and_wait_cb, T1_data);
    pthread_create(&handlers[1], NULL, lock_and_wait_cb, T2_data);
    pthread_create(&handlers[2], NULL, wait_and_unlock_cb, T3_data);

    pthread_exit(0);
}

int
main(int argc, char **argv){
    /*
     * Main tests will start. Cover the major cases.
     */
    printf("--- <Scenario 1> ---\n");
    prepare_assertion_failure();
    expected_failure_raised = false;
    test_unlock_without_locking();

    printf("--- <Scenario 2> ---\n");
    prepare_assertion_failure();
    expected_failure_raised = false;
    test_destory_rwl_with_lock();

    printf("--- <Scenario 3> ---\n");
    prepare_assertion_failure();
    expected_failure_raised = false;
    test_unregistered_thread_unlocking();

    return 0;
}

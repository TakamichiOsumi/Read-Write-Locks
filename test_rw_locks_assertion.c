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

    return 0;
}

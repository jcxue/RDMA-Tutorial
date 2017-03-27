#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#include "debug.h"
#include "config.h"
#include "setup_ib.h"
#include "ib.h"
#include "client.h"

void *client_thread_func (void *arg)
{
    int         ret		 = 0, n = 0;
    long	thread_id	 = (long) arg;
    int         msg_size	 = config_info.msg_size;

    pthread_t   self;
    cpu_set_t   cpuset;

    int                  num_wc       = 20;
    struct ibv_qp	*qp	      = ib_res.qp;
    struct ibv_cq       *cq           = ib_res.cq;
    struct ibv_wc       *wc           = NULL;
    uint32_t             lkey	      = ib_res.mr->lkey;
    char		*buf_ptr      = ib_res.ib_buf;
    int			 buf_offset   = 0;
    size_t               buf_size     = ib_res.ib_buf_size - msg_size;
    uint32_t             rkey	      = ib_res.rkey;
    uint64_t             raddr_base   = ib_res.raddr;
    uint64_t             raddr        = raddr_base;
    volatile char       *msg_start    = buf_ptr;
    volatile char       *msg_end      = msg_start + msg_size - 1;
    char                *send_buf_ptr = buf_ptr + buf_size;

    struct timeval      start, end;
    long                ops_count  = 0;
    double              duration   = 0.0;
    double              throughput = 0.0;

    wc = (struct ibv_wc *) calloc (num_wc, sizeof(struct ibv_wc));
    check (wc != NULL, "thread[%ld]: failed to allocate wc.", thread_id);

    /* set thread affinity */
    CPU_ZERO (&cpuset);
    CPU_SET  ((int)thread_id, &cpuset);
    self = pthread_self ();
    ret  = pthread_setaffinity_np (self, sizeof(cpu_set_t), &cpuset);
    check (ret == 0, "thread[%ld]: failed to set thread affinity", thread_id);


    while (ops_count < TOT_NUM_OPS) {
	/* loop till receive a msg from server */
	while ((*msg_start != 'A') && (*msg_end != 'A')) {
	}

	/* reset recv buffer */
	memset ((char *)msg_start, '\0', msg_size);

	/* send a msg back to the server */
	ops_count += 1;
	if ((ops_count % SIG_INTERVAL) == 0) {
	    ret = post_write_signaled (msg_size, lkey, 0, qp, send_buf_ptr, raddr, rkey);
	} else {
	    ret = post_write_unsignaled (msg_size, lkey, 0, qp, send_buf_ptr, raddr, rkey);
	}
	
	buf_offset = (buf_offset + msg_size) % buf_size;
	msg_start  = buf_ptr + buf_offset;
	msg_end    = msg_start + msg_size - 1;
	raddr      = raddr_base + buf_offset;

	if (ops_count == NUM_WARMING_UP_OPS) {
	    gettimeofday (&start, NULL);
	}

	n = ibv_poll_cq (cq, num_wc, wc);
	debug ("ops_count = %ld", ops_count);
    }

    gettimeofday (&end, NULL);
    /* dump statistics */
    duration   = (double)((end.tv_sec - start.tv_sec) * 1000000 + 
			  (end.tv_usec - start.tv_usec));
    throughput = (double)(ops_count) / duration;
    log ("thread[%ld]: throughput = %f (Mops/s)",  thread_id, throughput);

    free (wc);
    pthread_exit ((void *)0);

 error:
    if (wc != NULL) {
    	free (wc);
    }
    pthread_exit ((void *)-1);
}

int run_client ()
{
    int		ret	    = 0;
    long	num_threads = 1;
    long	i	    = 0;
    
    pthread_t	   *client_threads = NULL;
    pthread_attr_t  attr;
    void	   *status;

    log (LOG_SUB_HEADER, "Run Client");
    
    /* initialize threads */
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

    client_threads = (pthread_t *) calloc (num_threads, sizeof(pthread_t));
    check (client_threads != NULL, "Failed to allocate client_threads.");

    for (i = 0; i < num_threads; i++) {
	ret = pthread_create (&client_threads[i], &attr, 
			      client_thread_func, (void *)i);
	check (ret == 0, "Failed to create client_thread[%ld]", i);
    }

    bool thread_ret_normally = true;
    for (i = 0; i < num_threads; i++) {
	ret = pthread_join (client_threads[i], &status);
	check (ret == 0, "Failed to join client_thread[%ld].", i);
	if ((long)status != 0) {
            thread_ret_normally = false;
            log ("thread[%ld]: failed to execute", i);
        }
    }

    if (thread_ret_normally == false) {
        goto error;
    }

    pthread_attr_destroy (&attr);
    free (client_threads);
    return 0;

 error:
    if (client_threads != NULL) {
        free (client_threads);
    }
    
    pthread_attr_destroy (&attr);
    return -1;
}

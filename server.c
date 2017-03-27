#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#include "debug.h"
#include "ib.h"
#include "setup_ib.h"
#include "config.h"
#include "server.h"

void *server_thread (void *arg)
{
    int         ret             = 0, i = 0, n = 0;
    long        thread_id       = (long) arg;
    int         num_concurr_msgs= config_info.num_concurr_msgs;
    int         msg_size        = config_info.msg_size;
    int         num_wc          = 20;
    bool        stop            = false;

    pthread_t   self;
    cpu_set_t   cpuset;

    struct ibv_qp       *qp         = ib_res.qp;
    struct ibv_cq       *cq         = ib_res.cq;
    struct ibv_wc       *wc         = NULL;
    uint32_t             lkey       = ib_res.mr->lkey;
    char                *buf_ptr    = ib_res.ib_buf;
    int                  buf_offset = 0;
    size_t               buf_size   = ib_res.ib_buf_size;

    struct timeval      start, end;
    long                ops_count  = 0;
    double              duration   = 0.0;
    double              throughput = 0.0;

    /* set thread affinity */
    CPU_ZERO (&cpuset);
    CPU_SET  ((int)thread_id, &cpuset);
    self = pthread_self ();
    ret  = pthread_setaffinity_np (self, sizeof(cpu_set_t), &cpuset);
    check (ret == 0, "thread[%ld]: failed to set thread affinity", thread_id);

    /* pre-post recvs */
    wc = (struct ibv_wc *) calloc (num_wc, sizeof(struct ibv_wc));
    check (wc != NULL, "thread[%ld]: failed to allocate wc", thread_id);

    for (i = 0; i < num_concurr_msgs; i++) {
        ret = post_recv (msg_size, lkey, (uint64_t)buf_ptr, qp, buf_ptr);
        check (ret == 0, "thread[%ld]: failed to post recv", thread_id);
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr += buf_offset;
    }

    /* signal the client to start */
    ret = post_send (0, lkey, 0, MSG_CTL_START, qp, buf_ptr);
    check (ret == 0, "thread[%ld]: failed to signal the client to start", thread_id);

    while (stop != true) {
        /* poll cq */
	n = ibv_poll_cq (cq, num_wc, wc);
	if (n < 0) {
            check (0, "thread[%ld]: Failed to poll cq", thread_id);
        }

        for (i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                if (wc[i].opcode == IBV_WC_SEND) {
                    check (0, "thread[%ld]: send failed status: %s",
                           thread_id, ibv_wc_status_str(wc[i].status));
                } else {
		    check (0, "thread[%ld]: recv failed status: %s",
                           thread_id, ibv_wc_status_str(wc[i].status));
		}
            }

            if (wc[i].opcode == IBV_WC_RECV) {
                ops_count += 1;
		debug ("ops_count = %ld", ops_count);

		if (ops_count == NUM_WARMING_UP_OPS) {
                    gettimeofday (&start, NULL);
                }
		if (ops_count == TOT_NUM_OPS) {
		    gettimeofday (&end, NULL);
		    stop = true;
		    break;
		}

		/* echo the message back */
                char *msg_ptr = (char *)wc[i].wr_id;
		post_send (msg_size, lkey, 0, MSG_REGULAR, qp, msg_ptr);

		/* post a new receive */
                post_recv (msg_size, lkey, wc[i].wr_id, qp, msg_ptr);
	    }
	}
    }

    /* signal the client to stop */
    ret = post_send (0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp, ib_res.ib_buf);
    check (ret == 0, "thread[%ld]: failed to signal the client to stop", thread_id);

    stop = false;
    while (stop != true) {
	/* poll cq */
	n = ibv_poll_cq (cq, num_wc, wc);
	if (n < 0) {
            check (0, "thread[%ld]: Failed to poll cq", thread_id);
        }

        for (i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                if (wc[i].opcode == IBV_WC_SEND) {
                    check (0, "thread[%ld]: send failed status: %s",
                           thread_id, ibv_wc_status_str(wc[i].status));
                } else {
		    check (0, "thread[%ld]: recv failed status: %s",
                           thread_id, ibv_wc_status_str(wc[i].status));
		}
            }

            if (wc[i].opcode == IBV_WC_SEND) {
		if (wc[i].wr_id == IB_WR_ID_STOP) {
		    stop = true;
		    break;
		}
	    }
	}
    }

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

int run_server ()
{
    int   ret         = 0;
    long  num_threads = 1;
    long  i           = 0;

    pthread_t           *threads = NULL;
    pthread_attr_t       attr;
    void                *status;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

    threads = (pthread_t *) calloc (num_threads, sizeof(pthread_t));
    check (threads != NULL, "Failed to allocate threads.");

    for (i = 0; i < num_threads; i++) {
	ret = pthread_create (&threads[i], &attr, server_thread, (void *)i);
	check (ret == 0, "Failed to create server_thread[%ld]", i);
    }

    bool thread_ret_normally = true;
    for (i = 0; i < num_threads; i++) {
        ret = pthread_join (threads[i], &status);
        check (ret == 0, "Failed to join thread[%ld].", i);
        if ((long)status != 0) {
            thread_ret_normally = false;
            log ("server_thread[%ld]: failed to execute", i);
        }
    }

    if (thread_ret_normally == false) {
        goto error;
    }

    pthread_attr_destroy    (&attr);
    free (threads);

    return 0;

 error:
    if (threads != NULL) {
        free (threads);
    }
    pthread_attr_destroy    (&attr);
    
    return -1;
}

#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>

#include "sock.h"
#include "ib.h"
#include "debug.h"
#include "config.h"
#include "setup_ib.h"

struct IBRes ib_res;

int connect_qp_server ()
{
    int			 ret		= 0, n = 0, i = 0;
    int                  num_peers      = config_info.num_clients;
    int			 sockfd		= 0;
    int			*peer_sockfd	= NULL;
    struct sockaddr_in	 peer_addr;
    socklen_t		 peer_addr_len	= sizeof(struct sockaddr_in);
    char sock_buf[64]			= {'\0'};
    struct QPInfo	*local_qp_info	= NULL;
    struct QPInfo	*remote_qp_info = NULL;

    sockfd = sock_create_bind(config_info.sock_port);
    check(sockfd > 0, "Failed to create server socket.");
    listen(sockfd, 5);

    peer_sockfd = (int *) calloc (num_peers, sizeof(int));
    check (peer_sockfd != NULL, "Failed to allocate peer_sockfd");

    for (i = 0; i < num_peers; i++) {
	peer_sockfd[i] = accept(sockfd, (struct sockaddr *)&peer_addr,
				&peer_addr_len);
	check (peer_sockfd[i] > 0, "Failed to create peer_sockfd[%d]", i);
    }

    /* init local qp_info */
    local_qp_info = (struct QPInfo *) calloc (num_peers, 
					      sizeof(struct QPInfo));
    check (local_qp_info != NULL, "Failed to allocate local_qp_info");

    for (i = 0; i < num_peers; i++) {
	local_qp_info[i].lid	= ib_res.port_attr.lid; 
	local_qp_info[i].qp_num = ib_res.qp[i]->qp_num;
	local_qp_info[i].rank   = config_info.rank;
    }

    /* get qp_info from client */
    remote_qp_info = (struct QPInfo *) calloc (num_peers, 
					       sizeof(struct QPInfo));
    check (remote_qp_info != NULL, "Failed to allocate remote_qp_info");

    for (i = 0; i < num_peers; i++) {
	ret = sock_get_qp_info (peer_sockfd[i], &remote_qp_info[i]);
	check (ret == 0, "Failed to get qp_info from client[%d]", i);
    }
    
    /* send qp_info to client */
    int peer_ind = -1;
    int j        = 0;
    for (i = 0; i < num_peers; i++) {
	peer_ind = -1;
	for (j = 0; j < num_peers; j++) {
	    if (remote_qp_info[j].rank == i) {
		peer_ind = j;
		break;
	    }
	}
	ret = sock_set_qp_info (peer_sockfd[i], &local_qp_info[peer_ind]);
	check (ret == 0, "Failed to send qp_info to client[%d]", peer_ind);
    }

    /* change send QP state to RTS */
    log (LOG_SUB_HEADER, "Start of IB Config");
    for (i = 0; i < num_peers; i++) {
	peer_ind = -1;
	for (j = 0; j < num_peers; j++) {
	    if (remote_qp_info[j].rank == i) {
		peer_ind = j;
		break;
	    }
	}
	ret = modify_qp_to_rts (ib_res.qp[peer_ind], 
				remote_qp_info[i].qp_num, 
				remote_qp_info[i].lid);
	check (ret == 0, "Failed to modify qp[%d] to rts", peer_ind);

	log ("\tqp[%"PRIu32"] <-> qp[%"PRIu32"]", 
	     ib_res.qp[peer_ind]->qp_num, remote_qp_info[i].qp_num);
    }
    log (LOG_SUB_HEADER, "End of IB Config");

    /* sync with clients */
    for (i = 0; i < num_peers; i++) {
	n = sock_read (peer_sockfd[i], sock_buf, sizeof(SOCK_SYNC_MSG));
	check (n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");
    }
    
    for (i = 0; i < num_peers; i++) {
	n = sock_write (peer_sockfd[i], sock_buf, sizeof(SOCK_SYNC_MSG));
	check (n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");
    }
	
    for (i = 0; i < num_peers; i++) {
	close (peer_sockfd[i]);
    }
    free (peer_sockfd);
    close (sockfd);
    
    return 0;

 error:
    if (peer_sockfd != NULL) {
	for (i = 0; i < num_peers; i++) {
	    if (peer_sockfd[i] > 0) {
		close (peer_sockfd[i]);
	    }
	}
	free (peer_sockfd);
    }
    if (sockfd > 0) {
	close (sockfd);
    }
    
    return -1;
}

int connect_qp_client ()
{
    int ret	       = 0, n = 0, i = 0;
    int num_peers      = ib_res.num_qps;
    int *peer_sockfd   = NULL;
    char sock_buf[64]  = {'\0'};

    struct QPInfo *local_qp_info  = NULL;
    struct QPInfo *remote_qp_info = NULL;

    peer_sockfd = (int *) calloc (num_peers, sizeof(int));
    check (peer_sockfd != NULL, "Failed to allocate peer_sockfd");

    for (i = 0; i < num_peers; i++) {
	peer_sockfd[i] = sock_create_connect (config_info.servers[i],
					      config_info.sock_port);
	check (peer_sockfd[i] > 0, "Failed to create peer_sockfd[%d]", i);
    }

    /* init local qp_info */
    local_qp_info = (struct QPInfo *) calloc (num_peers, 
					      sizeof(struct QPInfo));
    check (local_qp_info != NULL, "Failed to allocate local_qp_info");

    for (i = 0; i < num_peers; i++) {
	local_qp_info[i].lid     = ib_res.port_attr.lid; 
	local_qp_info[i].qp_num  = ib_res.qp[i]->qp_num; 
	local_qp_info[i].rank    = config_info.rank;
    }

    /* send qp_info to server */
    for (i = 0; i < num_peers; i++) {
	ret = sock_set_qp_info (peer_sockfd[i], &local_qp_info[i]);
	check (ret == 0, "Failed to send qp_info[%d] to server", i);
    }

    /* get qp_info from server */    
    remote_qp_info = (struct QPInfo *) calloc (num_peers, 
					       sizeof(struct QPInfo));
    check (remote_qp_info != NULL, "Failed to allocate remote_qp_info");

    for (i = 0; i < num_peers; i++) {
	ret = sock_get_qp_info (peer_sockfd[i], &remote_qp_info[i]);
	check (ret == 0, "Failed to get qp_info[%d] from server", i);
    }
    
    /* change QP state to RTS */
    /* send qp_info to client */
    int peer_ind = -1;
    int j        = 0;
    log (LOG_SUB_HEADER, "IB Config");
    for (i = 0; i < num_peers; i++) {
	peer_ind = -1;
	for (j = 0; j < num_peers; j++) {
	    if (remote_qp_info[j].rank == i) {
		peer_ind = j;
		break;
	    }
	}
	ret = modify_qp_to_rts (ib_res.qp[peer_ind], 
				remote_qp_info[i].qp_num, 
				remote_qp_info[i].lid);
	check (ret == 0, "Failed to modify qp[%d] to rts", peer_ind);
    
	log ("\tqp[%"PRIu32"] <-> qp[%"PRIu32"]", 
	     ib_res.qp[peer_ind]->qp_num, remote_qp_info[i].qp_num);
    }
    log (LOG_SUB_HEADER, "End of IB Config");

    /* sync with server */
    for (i = 0; i < num_peers; i++) {
	n = sock_write (peer_sockfd[i], sock_buf, sizeof(SOCK_SYNC_MSG));
	check (n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client[%d]", i);
    }
    
    for (i = 0; i < num_peers; i++) {
	n = sock_read (peer_sockfd[i], sock_buf, sizeof(SOCK_SYNC_MSG));
	check (n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");
    }

    for (i = 0; i < num_peers; i++) {
	close (peer_sockfd[i]);
    }
    free (peer_sockfd);

    free (local_qp_info);
    free (remote_qp_info);
    return 0;

 error:
    if (peer_sockfd != NULL) {
	for (i = 0; i < num_peers; i++) {
	    if (peer_sockfd[i] > 0) {
		close (peer_sockfd[i]);
	    }
	}
	free (peer_sockfd);
    }

    if (local_qp_info != NULL) {
	free (local_qp_info);
    }

    if (remote_qp_info != NULL) {
	free (remote_qp_info);
    }
    
    return -1;
}

int setup_ib ()
{
    int	ret		         = 0;
    int i                        = 0;
    struct ibv_device **dev_list = NULL;    
    memset (&ib_res, 0, sizeof(struct IBRes));

    if (config_info.is_server) {
	ib_res.num_qps = config_info.num_clients;
    } else {
	ib_res.num_qps = config_info.num_servers;
    }

    /* get IB device list */
    dev_list = ibv_get_device_list(NULL);
    check(dev_list != NULL, "Failed to get ib device list.");

    /* create IB context */
    ib_res.ctx = ibv_open_device(*dev_list);
    check(ib_res.ctx != NULL, "Failed to open ib device.");

    /* allocate protection domain */
    ib_res.pd = ibv_alloc_pd(ib_res.ctx);
    check(ib_res.pd != NULL, "Failed to allocate protection domain.");

    /* query IB port attribute */
    ret = ibv_query_port(ib_res.ctx, IB_PORT, &ib_res.port_attr);
    check(ret == 0, "Failed to query IB port information.");
    
    /* register mr */
    /* set the buf_size twice as large as msg_size * num_concurr_msgs */
    /* the recv buffer occupies the first half while the sending buffer */
    /* occupies the second half */
    /* assume all msgs are of the same content */
    ib_res.ib_buf_size = config_info.msg_size * config_info.num_concurr_msgs * ib_res.num_qps;
    ib_res.ib_buf      = (char *) memalign (4096, ib_res.ib_buf_size);
    check (ib_res.ib_buf != NULL, "Failed to allocate ib_buf");

    ib_res.mr = ibv_reg_mr (ib_res.pd, (void *)ib_res.ib_buf,
			    ib_res.ib_buf_size,
			    IBV_ACCESS_LOCAL_WRITE |
			    IBV_ACCESS_REMOTE_READ |
			    IBV_ACCESS_REMOTE_WRITE);
    check (ib_res.mr != NULL, "Failed to register mr");
    
    /* query IB device attr */
    ret = ibv_query_device(ib_res.ctx, &ib_res.dev_attr);
    check(ret==0, "Failed to query device");
    
    /* create cq */
    ib_res.cq = ibv_create_cq (ib_res.ctx, ib_res.dev_attr.max_cqe, 
			       NULL, NULL, 0);
    check (ib_res.cq != NULL, "Failed to create cq");

    /* create srq */
    struct ibv_srq_init_attr srq_init_attr = {
	.attr.max_wr  = ib_res.dev_attr.max_srq_wr,
	.attr.max_sge = 1,
    };

    ib_res.srq = ibv_create_srq (ib_res.pd, &srq_init_attr);

    /* create qp */
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ib_res.cq,
        .recv_cq = ib_res.cq,
	.srq     = ib_res.srq,
        .cap = {
            .max_send_wr = ib_res.dev_attr.max_qp_wr,
            .max_recv_wr = ib_res.dev_attr.max_qp_wr,
            .max_send_sge = 1,
            .max_recv_sge = 1,
        },
        .qp_type = IBV_QPT_RC,
    };

    ib_res.qp = (struct ibv_qp **) calloc (ib_res.num_qps, 
					   sizeof(struct ibv_qp *));
    check (ib_res.qp != NULL, "Failed to allocate qp");

    for (i = 0; i < ib_res.num_qps; i++) {
	ib_res.qp[i] = ibv_create_qp (ib_res.pd, &qp_init_attr);
	check (ib_res.qp[i] != NULL, "Failed to create qp[%d]", i);
    }

    /* connect QP */
    if (config_info.is_server) {
	ret = connect_qp_server ();
    } else {
	ret = connect_qp_client ();
    }
    check (ret == 0, "Failed to connect qp");

    ibv_free_device_list (dev_list);
    return 0;

 error:
    if (dev_list != NULL) {
	ibv_free_device_list (dev_list);
    }
    return -1;
}

void close_ib_connection ()
{
    int i;

    if (ib_res.qp != NULL) {
	for (i = 0; i < ib_res.num_qps; i++) {
	    if (ib_res.qp[i] != NULL) {
		ibv_destroy_qp (ib_res.qp[i]);
	    }
	}
	free (ib_res.qp);
    }

    if (ib_res.srq != NULL) {
	ibv_destroy_srq (ib_res.srq);
    }

    if (ib_res.cq != NULL) {
	ibv_destroy_cq (ib_res.cq);
    }

    if (ib_res.mr != NULL) {
	ibv_dereg_mr (ib_res.mr);
    }

    if (ib_res.pd != NULL) {
        ibv_dealloc_pd (ib_res.pd);
    }

    if (ib_res.ctx != NULL) {
        ibv_close_device (ib_res.ctx);
    }

    if (ib_res.ib_buf != NULL) {
	free (ib_res.ib_buf);
    }
}

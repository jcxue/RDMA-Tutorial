#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/utsname.h>

#include "debug.h"
#include "config.h"

struct ConfigInfo config_info;

/* remove space, tab and line return from the line */
void clean_up_line (char *line)
{
    char *i = line;
    char *j = line;

    while (*j != 0) {
        *i = *j;
        j += 1;
        if (*i != ' ' && *i != '\t' && *i != '\r' && *i != '\n') {
            i += 1;
        }
    }
    *i = 0;
}

/*
 *  parse_node_list:
 *       get a list of servers or clients
 *
 *  return value:
 *       the number of nodes in the list
 */

int parse_node_list (char *line, char ***node_list)
{
    int start = 0, end = 0, num_nodes=0;
    char *i = line;
    char node_name_prefix[128] = {'\0'};
    char *j = node_name_prefix;

    while (*i != '.') {
        if ((*i >= '0') && (*i <= '9')) {
            start = start * 10 + *i - '0';
        } else {
            *j = *i;
            j += 1;
        }
        i += 1;
    }

    i += 2;
    while (*i != 0) {
        if ((*i >= '0') && (*i <= '9')) {
            end = end * 10 + *i - '0';
        }
        i += 1;
    }

    num_nodes = end - start + 1;
    check (num_nodes > 0, "Invaild number of nodes: %d", num_nodes);

    *node_list = (char **) calloc (num_nodes, sizeof(char *));
    if (*node_list == NULL){
        printf ("Failed to allocate node_list.\n");
        return 0;
    }

    int k = 0, node_ind = start;
    
    for (k = 0; k < num_nodes; k++) {
        (*node_list)[k] = (char *) calloc (128, sizeof(char));
        check ((*node_list)[k] != NULL,
               "Failed to allocate node_list[%d]", k);

        if (strstr(node_name_prefix, "mnemosyne")) {
            sprintf ((*node_list)[k], "mnemosyne%02d", node_ind);
        } else {
            sprintf ((*node_list)[k], "saguaro%d", node_ind);
        }

        node_ind += 1;
    }

    return num_nodes;

 error:
    return -1;
}

int get_rank ()
{
    int			ret	    = 0;
    uint32_t		i	    = 0;
    uint32_t		num_servers = config_info.num_servers;
    uint32_t		num_clients = config_info.num_clients;
    struct utsname	utsname_buf;
    char		hostname[64];

    /* get hostname */
    ret = uname (&utsname_buf);
    check (ret == 0, "Failed to call uname");

    strncpy (hostname, utsname_buf.nodename, sizeof(hostname));

    config_info.rank = -1;
    for (i = 0; i < num_servers; i++) {
        if (strstr(hostname, config_info.servers[i])) {
            config_info.rank      = i;
            config_info.is_server = true;
            break;
        }
    }

    for (i = 0; i < num_clients; i++) {
        if (strstr(hostname, config_info.clients[i])) {
            if (config_info.rank == -1) {
                config_info.rank      = i;
                config_info.is_server = false;
                break;
            } else {
                check (0, "node (%s) listed as both server and client", hostname);
            }
        }
    }

    check (config_info.rank >= 0, "Failed to get rank for node: %s", hostname);

    return 0;
 error:
    return -1;
}

int parse_config_file (char *fname)
{
    int ret = 0;
    FILE *fp = NULL;
    char line[128] = {'\0'};
    int  attr = 0;

    fp = fopen (fname, "r");
    check (fp != NULL, "Failed to open config file %s", fname);

    while (fgets(line, 128, fp) != NULL) {
        // skip comments
        if (strstr(line, "#") != NULL) {
            continue;
        }

        clean_up_line (line);

	if (strstr (line, "servers:")) {
            attr = ATTR_SERVERS;
            continue;
        } else if (strstr (line, "clients:")) {
            attr = ATTR_CLIENTS;
            continue;
        } else if (strstr (line, "msg_size:")) {
            attr = ATTR_MSG_SIZE;
            continue;
        } else if (strstr (line, "num_concurr_msgs:")) {
            attr = ATTR_NUM_CONCURR_MSGS;
            continue;
        }

	if (attr == ATTR_SERVERS) {
            ret = parse_node_list (line, &config_info.servers);
            check (ret > 0, "Failed to get server list");
            config_info.num_servers = ret;
        } else if (attr == ATTR_CLIENTS) {
            ret = parse_node_list (line, &config_info.clients);
            check (ret > 0, "Failed to get client list");
            config_info.num_clients = ret;
        } else if (attr == ATTR_MSG_SIZE) {
            config_info.msg_size = atoi(line);
            check (config_info.msg_size > 0,
                   "Invalid Value: msg_size = %d",
                   config_info.msg_size);
        } else if (attr == ATTR_NUM_CONCURR_MSGS) {
            config_info.num_concurr_msgs = atoi(line);
            check (config_info.num_concurr_msgs > 0,
                   "Invalid Value: num_concurr_msgs = %d",
                   config_info.num_concurr_msgs);
        }

        attr = 0;
    }

    ret = get_rank ();
    check (ret == 0, "Failed to get rank");

    fclose (fp);

    return 0;

 error:
    if (fp != NULL) {
        fclose (fp);
    }
    return -1;
}

void destroy_config_info ()
{
    int num_servers = config_info.num_servers;
    int num_clients = config_info.num_clients;
    int i;

    if (config_info.servers != NULL) {
        for (i = 0; i < num_servers; i++) {
            if (config_info.servers[i] != NULL) {
                free (config_info.servers[i]);
            }
        }
        free (config_info.servers);
    }

    if (config_info.clients != NULL) {
        for (i = 0; i < num_clients; i++) {
            if (config_info.clients[i] != NULL) {
                free (config_info.clients[i]);
            }
        }
        free (config_info.clients);
    }
}

void print_config_info ()
{
    log (LOG_SUB_HEADER, "Configuraion");

    if (config_info.is_server) {
	log ("is_server                 = %s", "true");
    } else {
	log ("is_server                 = %s", "false");
    }
    log ("rank                      = %d", config_info.rank);
    log ("msg_size                  = %d", config_info.msg_size);
    log ("num_concurr_msgs          = %d", config_info.num_concurr_msgs);
    log ("sock_port                 = %s", config_info.sock_port);
    
    log (LOG_SUB_HEADER, "End of Configuraion");
}

#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>
#include <inttypes.h>

enum ConfigFileAttr {
    ATTR_SERVERS = 1,
    ATTR_CLIENTS,
    ATTR_MSG_SIZE,
    ATTR_NUM_CONCURR_MSGS,
};

struct ConfigInfo {
    int  num_servers;
    int  num_clients;
    char **servers;          /* list of servers */
    char **clients;          /* list of clients */
    
    bool is_server;          /* if the current node is server */
    int  rank;               /* the rank of the node */

    int  msg_size;           /* the size of each echo message */
    int  num_concurr_msgs;   /* the number of messages can be sent concurrently */

    char *sock_port;         /* socket port number */
}__attribute__((aligned(64)));

extern struct ConfigInfo config_info;

int  parse_config_file   (char *fname);
void destroy_config_info ();

void print_config_info ();

#endif /* CONFIG_H_*/

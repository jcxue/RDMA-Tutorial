#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "config.h"

struct ConfigInfo config_info;

void print_config_info ()
{
    log (LOG_SUB_HEADER, "Configuraion");

    if (config_info.is_server) {
	log ("is_server                 = %s", "true");
    } else {
	log ("is_server                 = %s", "false");
    }
    log ("msg_size                  = %d", config_info.msg_size);
    log ("num_concurr_msgs          = %d", config_info.num_concurr_msgs);
    log ("sock_port                 = %s", config_info.sock_port);
    if (config_info.is_server == false) {
	log ("server_name               = %s", config_info.server_name);
    }

    log (LOG_SUB_HEADER, "End of Configuraion");
}

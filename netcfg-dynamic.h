#ifndef _NETCFG_DYNAMIC_H_
#define _NETCFG_DYNAMIC_H_

#include "netcfg.h"

struct interface_config_dynamic
{
	struct interface_config config_common;

	enum { DHCP_PUMP, DHCP_DHCLIENT, DHCP_UDHCPC } dhcp_client;
};

#endif /* _NETCFG_DYNAMIC_H_ */

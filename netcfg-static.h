#ifndef _NETCFG_STATIC_H_
#define _NETCFG_STATIC_H_

#include "netcfg.h"

struct interface_config_static
{
	struct interface_config config_common;

	struct common
	{
		enum { PTP_GATEWAY_NO, PTP_GATEWAY_YES } pointopoint_gateway;
	}
	common;

	struct inet
	{
		struct in_addr address, gateway;
		short netmask;
		short no_gateway;
	}
	inet;

	struct inet6
	{
		struct in6_addr address, gateway;
		short netmask;
		short no_gateway;
	}
	inet6;
};

#endif /* _NETCFG_STATIC_H_ */

/* 
   netcfg-dynamic.c - Configure a dynamic network for the debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
                 2002  Bastian Blank <waldi@debian.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
   
*/

#include <ctype.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cdebconf/debconfclient.h>
#include <debian-installer.h>

#include "netcfg-dynamic.h"

static int netcfg_dynamic_check_dhcp (struct interface_config_dynamic *config);
static void netcfg_dynamic_get_loop_inet (struct interface_config_dynamic *config, int af);
static int netcfg_dynamic_get_inet (struct interface_config_dynamic *config, int af);

static void netcfg_dynamic_get (struct interface_config_dynamic *config)
{
	netcfg_common_get (&config->config_common);

	if (config->config_common.inet && netcfg_dynamic_check_dhcp (config))
		netcfg_dynamic_get_loop_inet (config, AF_INET);
	else
		config->config_common.inet = 0;
	if (config->config_common.inet6)
		netcfg_dynamic_get_loop_inet (config, AF_INET6);

	netcfg_common_interface_configured_add (&config->config_common);
}

static void netcfg_dynamic_get_loop_inet (struct interface_config_dynamic *config, int af)
{
	while (1)
	{
		if (!netcfg_dynamic_get_inet (config, af))
			break;
	}

	if (af == AF_INET)
		debconf_interface_set (config->config_common.interface, "common", "families/inet", "true");
	else
		debconf_interface_set (config->config_common.interface, "common", "families/inet6", "true");
}

static int netcfg_dynamic_get_inet (struct interface_config_dynamic *config, int af)
{
	char *ptr = NULL;

	if (af == AF_INET)
		debconf_input ("high", "dynamic/do_dhcp");
	else
		debconf_input ("high", "dynamic/inet6/do");

	if (!ptr || strstr (ptr, "true"))
		return 0;

	return -1;
}

static int netcfg_dynamic_check_dhcp (struct interface_config_dynamic *config)
{
        struct stat buf;

	if (stat ("/sbin/dhclient", &buf) == 0)
		config->dhcp_client = DHCP_DHCLIENT;
	else if (stat ("/sbin/pump", &buf) == 0)
		config->dhcp_client = DHCP_PUMP;
	else if (stat ("/sbin/udhcpc", &buf) == 0)
		config->dhcp_client = DHCP_UDHCPC;
	else
	{
		client->command (client, "input", "critical", TEMPLATE_PREFIX "dynamic/no_dhcp_client", NULL);
		client->command (client, "go", NULL);
		return 1;
	}
	return 0;
}

static int netcfg_dynamic_activate (struct interface_config_dynamic *config)
{
	int rv = 0;
	char buf[128], *ptr;
	int inet6 = 0;

	ptr = debconf_interface_get (config->config_common.interface, "common", "families/inet6");
	if (strstr (ptr, "true"))
		inet6 = 1;

	if (inet6)
	{
		snprintf (buf, sizeof (buf), "/bin/ip link set %s up", config->config_common.interface);
		rv |= di_execlog (buf);
	}

	if (rv)
		return 1;
	return 0;
}

static int netcfg_dynamic_activate_dhcp (struct interface_config_dynamic *config)
{
	int rv = 0;
	char buf[128], *ptr;
	int inet = 0;

	ptr = debconf_interface_get (config->config_common.interface, "common", "families/inet");
	if (strstr (ptr, "true"))
		inet = 1;

	debconf_input ("medium", "debian-installer/netcfg/dynamic/do_dhcp");

	if (inet)
	{
		switch (config->dhcp_client)
		{
			case DHCP_PUMP:
				snprintf (buf, sizeof (buf), "/sbin/pump -i %s", config->config_common.interface);
				break;

			case DHCP_DHCLIENT:
				snprintf (buf, sizeof (buf), "/sbin/dhclient %s", config->config_common.interface);
				break;

			case DHCP_UDHCPC:
				snprintf (buf, sizeof (buf), "/sbin/udhcpc -i %s -n", config->config_common.interface);
				break;
		}

		rv |= di_execlog (buf);
	}

	if (rv)
		return 1;
	return 0;
}

#if 0
static void netcfg_dynamic_write ()
{
        FILE *fp;

        if ((fp = file_open(INTERFACES_FILE))) {
                fprintf(fp,
                        "\n# This entry was created during the Debian installation\n");
                fprintf(fp, "iface %s inet dhcp\n", interface);
                if (dhcp_hostname)
                        fprintf(fp, "\thostname %s\n", dhcp_hostname);
                fclose(fp);
        }
}
#endif

int main (int argc, char *argv[])
{
	static struct interface_config_dynamic config;
	int need_nameserver = 2;

	client = debconfclient_new ();
	client->command (client, "title", "Dynamic Network Configuration", NULL);

	netcfg_common_get_hostname (&config.config_common);

	while (1)
	{
		if (!netcfg_common_get_interface (&config.config_common))
			break;

		if (config.config_common.info->pointopoint == PTP_YES)
			continue;

		netcfg_common_set_type (&config.config_common, "dynamic");
		netcfg_dynamic_get (&config);

		if (config.config_common.inet)
			need_nameserver = 0;

		if (need_nameserver == 2)
		{
			netcfg_common_get_nameserver (&config.config_common);
			need_nameserver = 1;
		}

		if (netcfg_dynamic_activate (&config) || netcfg_dynamic_activate_dhcp (&config))
			netcfg_die_cfg ();
	}

	if (!config.config_common.inet)
		netcfg_common_write_nameserver (&config.config_common);
	//netcfg_dynamic_write (&config);

	return 0;
}


/* 
   netcfg-dhcp.c - Configure a network via dhcp for the debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
   Copyright (C) 2003  Matt Kraai
   
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
#include "netcfg.h"

static u_int32_t ipaddress = 0;
static u_int32_t nameserver_array[4] = { 0 };
static struct debconfclient *client;
enum {
        PUMP,
        DHCLIENT,
        UDHCPC
} dhcp_client = PUMP;


static char *none;

static int netcfg_get_dhcp_hostname(struct debconfclient *client, char **dhcp_hostname)
{
	int ret;

        debconf_input(client, "low", "netcfg/dhcp_hostname");
        ret = debconf_go(client);
	if (ret == 30) 
		return ret;
        if (*dhcp_hostname) {
                free(*dhcp_hostname);
                *dhcp_hostname = NULL;
        }
        debconf_get(client, "netcfg/dhcp_hostname");

        if (strcmp (client->value, "") != 0)
                *dhcp_hostname = strdup(client->value);
	return 0;
}


static void netcfg_write_dhcp(char *interface, char *dhcp_hostname)
{

        FILE *fp;

        if ((fp = file_open(INTERFACES_FILE, "a"))) {
                fprintf(fp,
                        "\n# This entry was created during the Debian installation\n");
		fprintf(fp, "auto %s\n", interface);
                fprintf(fp, "iface %s inet dhcp\n", interface);
                if (dhcp_hostname)
                        fprintf(fp, "\thostname %s\n", dhcp_hostname);
                fclose(fp);
        }
}


static void netcfg_activate_dhcp(struct debconfclient *client,
				 char *interface, char *dhcp_hostname)
{
        char buf[128];
        di_exec_shell_log("/sbin/ifconfig lo 127.0.0.1");
	di_exec_shell_log("/sbin/modprobe af_packet");

        switch (dhcp_client) {
        case PUMP:
                snprintf(buf, sizeof(buf), "/sbin/pump -i %s", interface);
                if (dhcp_hostname)
                        di_snprintfcat(buf, sizeof(buf), " -h %s",
                                       dhcp_hostname);
                break;

        case DHCLIENT:
                snprintf(buf, sizeof(buf), "/sbin/dhclient -e %s", interface);
                break;

        case UDHCPC:
                snprintf(buf, sizeof(buf), "/sbin/udhcpc -i %s -n",
                         interface);
                if (dhcp_hostname)
                        di_snprintfcat(buf, sizeof(buf), " -H %s",
                                       dhcp_hostname);
                break;
        }

        if (di_exec_shell_log(buf))
                netcfg_die(client);
}


int main(int argc, char *argv[])
{
        char *ptr;
        struct stat buf;
	int num_interfaces;
	char *interface = NULL, *hostname = NULL,*domain = NULL, *dhcp_hostname = NULL;
       	enum { BACKUP, GET_INTERFACE, GET_HOSTNAME, GET_DHCP_HOSTNAME, 
	       QUIT } state = GET_INTERFACE;

	client = debconfclient_new();
	debconf_capb(client,"backup");

        debconf_metaget(client, "netcfg/internal-none", "description");
        none = client->value ? strdup(client->value) : strdup("<none>");

        if (stat("/sbin/dhclient", &buf) == 0)
                dhcp_client = DHCLIENT;
        else if (stat("/sbin/pump", &buf) == 0)
                dhcp_client = PUMP;
        else if (stat("/sbin/udhcpc", &buf) == 0)
                dhcp_client = UDHCPC;
        else {
                debconf_input(client, "critical", "netcfg/no_dhcp_client");
                debconf_go(client);
                exit(1);
        }

	while (state != QUIT) {
		switch (state) {
		case BACKUP:
			exit (10); // Back the whole way out
			break;
		case GET_INTERFACE:
			state =  netcfg_get_interface(client, &interface, &num_interfaces) ? 
			   	BACKUP : GET_HOSTNAME;
			break;
		case GET_HOSTNAME:					
			if (netcfg_get_hostname(client, &hostname)) 
				// if num_interfaces ==1 , interface question won't be asked. Go further back
				state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
			else
				state =  GET_DHCP_HOSTNAME;
			break;
		case GET_DHCP_HOSTNAME:
			state = netcfg_get_dhcp_hostname(client, &dhcp_hostname) ?
				GET_HOSTNAME : QUIT;
			break;
		case QUIT:
			break;
		}
	}

        netcfg_write_common("40netcfg-dhcp", ipaddress, domain, hostname,
			    nameserver_array);
        netcfg_write_dhcp(interface, dhcp_hostname);
	debconf_progress_start(client, 0, 1, "netcfg/dhcp_progress");
	netcfg_progress_displayed = 1;
	debconf_progress_info(client, "netcfg/dhcp_progress_note");
        my_debconf_input(client, "medium", "netcfg/do_dhcp", &ptr);
        netcfg_activate_dhcp(client, interface, dhcp_hostname);
	debconf_progress_step(client, 1);
	debconf_progress_stop(client);
	netcfg_progress_displayed = 0;

        return 0;
}

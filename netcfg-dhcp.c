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

static char *interface = NULL;
static char *hostname = NULL;
static char *domain = NULL;
static u_int32_t ipaddress = 0;
static u_int32_t nameserver_array[4] = { 0 };
static struct debconfclient *client;
enum {
        PUMP,
        DHCLIENT,
        UDHCPC
} dhcp_client_choices;

int dhcp_client = PUMP;

static char *dhcp_hostname = NULL;
static char *none;

static char *my_debconf_input(char *priority, char *template)
{
        debconf_fset(client, template, "seen", "false");
        debconf_input(client, priority, template);
        debconf_go(client);
        debconf_get(client, template);
        return client->value;
}

static void netcfg_get_dhcp()
{
        if (dhcp_hostname) {
                free(dhcp_hostname);
                dhcp_hostname = NULL;
        }

        debconf_input(client, "low", "netcfg/dhcp_hostname");
        debconf_go(client);
        debconf_get(client, "netcfg/dhcp_hostname");

        if (strcmp (client->value, "") != 0)
                dhcp_hostname = strdup(client->value);

        debconf_subst(client, "netcfg/confirm_dhcp", "dhcp_hostname",
                     (dhcp_hostname ? dhcp_hostname : none));
}


static void netcfg_write_dhcp()
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


static void netcfg_activate_dhcp()
{
        char buf[128];
        di_execlog("/sbin/ifconfig lo 127.0.0.1");
	di_execlog("/sbin/modprobe af_packet");

        switch (dhcp_client) {
        case PUMP:
                snprintf(buf, sizeof(buf), "/sbin/pump -i %s", interface);
                if (dhcp_hostname)
                        di_snprintfcat(buf, sizeof(buf), " -h %s",
                                       dhcp_hostname);
                break;

        case DHCLIENT:
                snprintf(buf, sizeof(buf), "/sbin/dhclient %s", interface);
                break;

        case UDHCPC:
                snprintf(buf, sizeof(buf), "/sbin/udhcpc -i %s -n",
                         interface);
                if (dhcp_hostname)
                        di_snprintfcat(buf, sizeof(buf), " -H %s",
                                       dhcp_hostname);
                break;
        }

        if (di_execlog(buf))
                netcfg_die(client);
}

int main(int argc, char *argv[])
{
        char *ptr;
        char *nameservers = NULL;
        int finished = 0;
        struct stat buf;
        client = debconfclient_new();
        // client->command(client, "SETTITLE", "netcfg/dhcp-title", NULL);

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


        do {
                netcfg_get_interface(client, &interface);
		netcfg_get_hostname(client, &hostname);

                debconf_subst(client, "netcfg/confirm_dhcp", "interface", interface);

                debconf_subst(client, "netcfg/confirm_dhcp", "hostname", hostname);

                debconf_subst(client, "netcfg/confirm_dhcp", "domain", 
		         	(domain ? domain : none));

                netcfg_nameservers_to_array(nameservers, nameserver_array);

                debconf_subst(client, "netcfg/confirm_dhcp", "nameservers",
                                (nameservers ? nameservers : none));
                netcfg_get_dhcp();


                ptr = my_debconf_input("medium", "netcfg/confirm_dhcp");

                if (strstr(ptr, "true"))
                        finished = 1;
        }
        while (!finished);

        netcfg_write_common("40netcfg-dhcp", ipaddress, domain, hostname,
			    nameserver_array);
        netcfg_write_dhcp();

	debconf_progress_start(client, 0, 1, "netcfg/dhcp_progress");
	netcfg_progress_displayed = 1;
	debconf_progress_info(client, "netcfg/dhcp_progress_note");
        my_debconf_input("medium", "netcfg/do_dhcp");
        netcfg_activate_dhcp();
	debconf_progress_step(client, 1);
	debconf_progress_stop(client);
	netcfg_progress_displayed = 0;

        return 0;
}

/* 
   netcfg-static.c - Configure a static network for the debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
   
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
static u_int32_t network = 0;
static u_int32_t broadcast = 0;
static u_int32_t netmask = 0;
static u_int32_t gateway = 0;
static u_int32_t pointopoint = 0;
static struct debconfclient *client;
static char *none;

static int netcfg_get_static()
{
        char *ptr;
	int ret = 0;
	enum { STATE_QUIT, STATE_GET_IPADDR, STATE_GET_POINTTOPOINT, STATE_GET_NETMASK,
		STATE_GET_GATEWAY, STATE_GATEWAY_UNREACHABLE } state = STATE_GET_IPADDR;

        ipaddress = network = broadcast = netmask = gateway = pointopoint =
            0;

	/* Simple state machine to handle goback buttons */
	while (state != STATE_QUIT) {
		switch (state) { 
	
		case STATE_GET_IPADDR:
	       		 ret = my_debconf_input(client,"critical", "netcfg/get_ipaddress", &ptr);
			if (ret == 30)  {
				/* back the whole way out */
				return 30;
			 } else {
				dot2num(&ipaddress, ptr);
				debconf_subst(client, "netcfg/confirm_static", "ipaddress",
				  	      (ipaddress ? num2dot(ipaddress) : none));
				 if (strncmp(interface, "plip", 4) == 0
				     || strncmp(interface, "slip", 4) == 0
	       		              || strncmp(interface, "ctc", 3) == 0
	       		              || strncmp(interface, "escon", 5) == 0)
					 state = STATE_GET_POINTTOPOINT;
				 else
					 state = STATE_GET_NETMASK;
			}
			break;
	
		case STATE_GET_POINTTOPOINT:
       		        ret = my_debconf_input(client,"critical", "netcfg/get_pointopoint", &ptr);
			if (ret == 30) {
				state = STATE_GET_IPADDR;
			} else { 
		                dot2num(&pointopoint, ptr);
       		         	dot2num(&netmask, "255.255.255.255");
		                network = ipaddress;
		                gateway = pointopoint;
				state = STATE_QUIT;
			}
			break;
	
		case  STATE_GET_NETMASK:
			ret = my_debconf_input(client,"critical", "netcfg/get_netmask", &ptr);
			if (ret == 30) {
				state = STATE_GET_IPADDR;
			} else {
				dot2num(&netmask, ptr);
				gateway = ipaddress & netmask;
				debconf_set(client, "netcfg/get_gateway", num2dot(gateway+1));
				state = STATE_GET_GATEWAY;
			}
			break;
	
		case STATE_GET_GATEWAY:
       		        ret  = my_debconf_input(client, "critical", "netcfg/get_gateway", &ptr);
			if (ret == 30) {
				state = STATE_GET_NETMASK;
			} else {
       		         	dot2num(&gateway, ptr);
	                	network = ipaddress & netmask;
       		         	if (gateway && ((gateway & netmask) != network)) 
					state = STATE_GATEWAY_UNREACHABLE;
				else
					state = STATE_QUIT;
			}
			break;
	
		case STATE_GATEWAY_UNREACHABLE:
			debconf_input(client, "high", "netcfg/gateway_unreachable");
       		        debconf_go(client);
			state = STATE_GET_GATEWAY;
			break;

		case STATE_QUIT:
			break;
        	}
		
	}

        debconf_subst(client, "netcfg/confirm_static", "netmask", 
		      (netmask ? num2dot(netmask) : none));

        debconf_subst(client, "netcfg/confirm_static", "gateway", 
		      (gateway ? num2dot(gateway) : none));

        debconf_subst(client, "netcfg/confirm_static", "pointopoint",
                        (pointopoint ? num2dot(pointopoint) : none));

        broadcast = (network | ~netmask);
	return 0;
}


static int netcfg_write_static()
{
        FILE *fp;

        if ((fp = file_open(NETWORKS_FILE, "w"))) {
                fprintf(fp, "localnet %s\n", num2dot(network));
                fclose(fp);

		di_system_prebaseconfig_append("40netcfg-static", "cp %s %s\n",
					NETWORKS_FILE,
					"/target" NETWORKS_FILE);
        } else
                goto error;

        if ((fp = file_open(INTERFACES_FILE, "a"))) {
                fprintf(fp,
                        "\n# This entry was created during the Debian installation\n");
                fprintf(fp,
                        "# (network, broadcast and gateway are optional)\n");
		fprintf(fp, "auto %s\n", interface);
                fprintf(fp, "iface %s inet static\n", interface);
                fprintf(fp, "\taddress %s\n", num2dot(ipaddress));
                fprintf(fp, "\tnetmask %s\n", num2dot(netmask));
                fprintf(fp, "\tnetwork %s\n", num2dot(network));
                fprintf(fp, "\tbroadcast %s\n", num2dot(broadcast));
                if (gateway)
                        fprintf(fp, "\tgateway %s\n", num2dot(gateway));
                if (pointopoint)
                        fprintf(fp, "\tpointopoint %s\n",
                                num2dot(pointopoint));
                fclose(fp);
        } else
                goto error;

        return 0;
      error:
        return -1;
}

static int netcfg_activate_static()
{
        int rv = 0;
        char buf[256];
#ifdef __GNU__
/* I had to do something like this ? */
/*  di_exec_shell_log ("settrans /servers/socket/2 -fg");  */
        di_exec_shell_log("settrans /servers/socket/2 --goaway");
        snprintf(buf, sizeof(buf),
                 "settrans -fg /servers/socket/2 /hurd/pfinet --interface=%s --address=%s",
                 interface, num2dot(ipaddress));
        di_snprintfcat(buf, sizeof(buf), " --netmask=%s",
                       num2dot(netmask));
        buf[sizeof(buf) - 1] = '\0';

        if (gateway)
                snprintf(buf, sizeof(buf), " --gateway=%s",
                         num2dot(gateway));

        rv |= di_exec_shell_log(buf);

#else
        di_exec_shell_log("/sbin/ifconfig lo 127.0.0.1");

        snprintf(buf, sizeof(buf), "/sbin/ifconfig %s %s",
                 interface, num2dot(ipaddress));
        di_snprintfcat(buf, sizeof(buf), " netmask %s", num2dot(netmask));
        di_snprintfcat(buf, sizeof(buf), " broadcast %s",
                       num2dot(broadcast));
        buf[sizeof(buf) - 1] = '\0';

        if (pointopoint)
                di_snprintfcat(buf, sizeof(buf), " pointopoint %s",
                               num2dot(pointopoint));

        rv |= di_exec_shell_log(buf);

        if (gateway) {
                snprintf(buf, sizeof(buf),
                         "/sbin/route add default gateway %s",
                         num2dot(gateway));
                rv |= di_exec_shell_log(buf);
        }
#endif

        if (rv != 0) {
                debconf_input(client, "critical", "netcfg/error_cfg");
                debconf_go(client);
        }
        return 0;
}

int main(int argc, char *argv[])
{
        int ret = 0, goback = 0;
        char *ptr;
        char *nameservers = NULL;
	enum { QUIT, GET_COMMON, GET_STATIC, CONFIRM_STATIC} state = GET_COMMON;

        client = debconfclient_new();
	debconf_capb(client, "backup");

        debconf_metaget(client,  "netcfg/internal-none", "description");
        none = client->value ? strdup(client->value) : strdup("<none>");

	while (state != QUIT) {
		switch (state) {

		case GET_COMMON:
	                ret = netcfg_get_common(client, &interface, &hostname, &domain,
        	                                &nameservers, goback);
			if (ret == 30) {
				exit (10); /* goback the whole way out */
			} else { 
				goback = 0;
				state = GET_STATIC;
			}
			break;
		
		case GET_STATIC:	
			ret = netcfg_get_static();
			if (ret == 30) {
				goback = 1;
				state = GET_COMMON;
			} else { 
				state = CONFIRM_STATIC;
			}
			break;

		case CONFIRM_STATIC:
               		debconf_subst(client, "netcfg/confirm_static", "interface", interface);
                	debconf_subst(client, "netcfg/confirm_static", "hostname", hostname);
                	debconf_subst(client, "netcfg/confirm_static", "domain", (domain ? domain : none));
       	        	debconf_subst(client, "netcfg/confirm_static", "nameservers",
               	        	      (nameservers ? nameservers : none));
               		netcfg_nameservers_to_array(nameservers, nameserver_array);

			debconf_capb(client); // Turn off backup for yes/no confirmation
	                my_debconf_input(client, "medium", "netcfg/confirm_static", &ptr);
	                state = strstr(ptr, "true") ? QUIT : GET_COMMON; 
			debconf_capb(client, "backup");
			break;
			
		case QUIT:
			break;
        	}
	}

        netcfg_write_common("40netcfg-static", ipaddress, domain, hostname,
			    nameserver_array);
        netcfg_write_static();
        netcfg_activate_static();

        return 0;
}

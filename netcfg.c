/* 
   netcfg.c - Configure a network via DHCP or manual configuration 
   for debian-installer

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

static method_t netcfg_method = DHCP;

response_t netcfg_get_method(struct debconfclient *client)
{
    int iret, ret;

    iret = debconf_input(client, "medium", "netcfg/use_dhcp");
    ret = debconf_go(client);

    debconf_get(client, "netcfg/use_dhcp");

    if (ret == 30)
      return GO_BACK;

    if (strcmp(client->value, "true") == 0)
      netcfg_method = DHCP;
    else 
      netcfg_method = STATIC;

    if (iret == 30)
      return NOT_ASKED;

    return 0;
}

int main(void)
{
    int num_interfaces = 0;
    enum { BACKUP, GET_HOSTNAME, GET_INTERFACE, GET_METHOD, GET_DHCP, GET_STATIC, WCONFIG, QUIT } state = GET_HOSTNAME;
    static struct debconfclient *client;
    static int requested_wireless_tools = 0;
    response_t res;

    /* initialize libd-i */
    di_system_init("netcfg");

    /* initialize debconf */
    client = debconfclient_new();
    debconf_capb(client, "backup");
    
    while (1) {
	switch(state) {
	case GET_HOSTNAME:
            if (netcfg_get_hostname(client, &hostname))
	      state = BACKUP;
	    else
	      state = GET_INTERFACE;
	    break;
	case BACKUP:
	    return 10;
	case GET_INTERFACE:
	    if(netcfg_get_interface(client, &interface, &num_interfaces))
	      state = GET_HOSTNAME;
	    else
	    {
	      if (is_wireless_iface (interface))
		state = WCONFIG;
	      else
		state = GET_METHOD;
	    }
	    break;
	case GET_METHOD:
	    if ((res = netcfg_get_method(client)) == GO_BACK)
		state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
	    else
	    {
	        method_t mii_result;
		
		ifconfig_up(interface);
		mii_result = mii_diag_status_lite(interface);
		ifconfig_down(interface);

		di_debug("mii_result = %s",
                    mii_result == DHCP ? "DHCP" :
                    ( mii_result == STATIC ? "static" : 
		      ( mii_result == DUNNO ? "dunno!" : "REALLY dunno")));

		di_debug("res = %s",
		    res == NOT_ASKED ? "not asked" :
		    ( res == GO_BACK ? "go back" : "unknown" ));

		/* Don't override the user's choice. */
		di_debug("netcfg_method = %s",
		    netcfg_method == DHCP ? "DHCP" :
		    ( netcfg_method == STATIC ? "static" :
		      ( netcfg_method == DUNNO ? "dunno!" : "REALLY dunno")));
		
		if (mii_result != DUNNO && res == NOT_ASKED)
		  netcfg_method = mii_result;


		di_debug("netcfg_method = %s",
		    netcfg_method == DHCP ? "DHCP" :
		    ( netcfg_method == STATIC ? "static" : 
		      ( netcfg_method == DUNNO ? "dunno!" : "REALLY dunno")));

		if (netcfg_method == DHCP) 
		    state = GET_DHCP;
		else
		    state = GET_STATIC;
	    }
	    break;

	case GET_DHCP:
	        switch (netcfg_activate_dhcp(client)) {
                case 0:
		    state = QUIT;
		    break;
                case 30:
		    state = BACKUP;
		    break;
                default:
		    state = GET_STATIC;
		    break;
	        }
	    break;
	case GET_STATIC:
	    if (netcfg_get_static(client))
		state = GET_METHOD;
	    else {
		if (netcfg_activate_static(client))
		    exit(1);
		else
		    state = QUIT;
	    }
	    break;
	case WCONFIG:
	    if (requested_wireless_tools == 0)
	    {
	      di_exec_shell_log("apt-install wireless-tools");
	      requested_wireless_tools = 1;
	    }
	    /* Must make sure WEP question is always asked, independent
	     * of whether set essid question was asked. */
	    if (netcfg_wireless_set_essid (client, interface) == GO_BACK)
	    {
	      state = BACKUP;
	      break;
	    }
            
	    if (netcfg_wireless_set_wep (client, interface) == GO_BACK)
	    {
	      state = BACKUP;
	      break;
	    }
	    
	    state = GET_METHOD;
	    break;
	case QUIT:
	    return 0;
	}
    }

    return 0;
}

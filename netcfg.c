/* 
   netcfg.c - Configure a network via DHCP or manual configuration 
   for debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
                            and others (see debian/copyright)
   
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <cdebconf/debconfclient.h>
#include <debian-installer.h>
#include "netcfg.h"

static method_t netcfg_method = DHCP;

response_t netcfg_get_method(struct debconfclient *client)
{
    int iret, ret;

    iret = debconf_input(client, "medium", "netcfg/use_dhcp");
    ret = debconf_go(client);

    if (ret == 30)
      return GO_BACK;
    
    debconf_get(client, "netcfg/use_dhcp");

    if (strcmp(client->value, "true") == 0)
      netcfg_method = DHCP;
    else 
      netcfg_method = STATIC;

    if (iret == 30)
      return NOT_ASKED;

    return 0;
}

int main(int argc, char *argv[])
{
    int num_interfaces = 0;
    enum { BACKUP, GET_INTERFACE, GET_HOSTNAME_ONLY, GET_METHOD, GET_DHCP, GET_STATIC, WCONFIG, WCONFIG_ESSID, WCONFIG_WEP, QUIT } state = GET_INTERFACE;
    static struct debconfclient *client;
    static int requested_wireless_tools = 0;
    response_t res;

    /* initialize libd-i */
    di_system_init("netcfg");

    parse_args (argc, argv);
    reap_old_files ();
    open_sockets();

    /* initialize debconf */
    client = debconfclient_new();
    debconf_capb(client, "backup");

    /* always always always default back to DHCP, unless you've specified
     * disable_dhcp on the command line. */
    debconf_get(client, "netcfg/disable_dhcp");
    
    if (!strcmp(client->value, "true"))
      debconf_set(client, "netcfg/use_dhcp", "false");
    else
      debconf_set(client, "netcfg/use_dhcp", "true");

    for (;;)
    {
	switch(state)
	{
	case BACKUP:
	    return 10;
	case GET_INTERFACE:
	    if(netcfg_get_interface(client, &interface, &num_interfaces))
	      state = BACKUP;
	    else if (! interface || ! num_interfaces)
	      state = GET_HOSTNAME_ONLY;
	    else
	    {
	      if (is_wireless_iface (interface))
		state = WCONFIG;
	      else
		state = GET_METHOD;
	    }
	    break;
	case GET_HOSTNAME_ONLY:
	    if(netcfg_get_hostname(client, "netcfg/get_hostname", &hostname, 0))
	      state = BACKUP;
	    else
	    {
	      struct in_addr null_ipaddress;
	      null_ipaddress.s_addr = 0;
	      netcfg_write_common(null_ipaddress, hostname, NULL);
	      return 0;
	    }
	    break;
	case GET_METHOD:
	    if ((res = netcfg_get_method(client)) == GO_BACK)
		state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
	    else
	    {
              char buf[64] = { 0 };
              int ret;

              snprintf(buf, 64, "mii-diag -s %s", interface);

              interface_up(interface);
              sleep(2);
	      di_info("Executing: %s", buf);
	      ret = di_exec_shell_log(buf);
              interface_down(interface);

	      ret = di_exec_mangle_status(ret);

              di_info("link status for %s is: %s (%d)", interface,
                  (ret == 1) ? "unknown" :
                  ((ret == 2) ? "disconnected" :
                  ((ret == 0) ? "connected" : "unknown")), ret);

              if (res == NOT_ASKED)
              {
                switch (ret)
                {
                  /* Supported; no connection */
                  case 2:
                    netcfg_method = STATIC;
                    break;

                  /* Supported; connected */    
                  case 0:
                    netcfg_method = DHCP;
                    break;
                }
              }
              
              if (netcfg_method == DHCP) 
                state = GET_DHCP;
              else
                state = GET_STATIC;
	    }
	    break;

	case GET_DHCP:
            switch (netcfg_activate_dhcp(client))
            {
              case 0:
                state = QUIT;
                break;
              case 30:
                state = BACKUP;
                break;
              case 15:
                state = GET_STATIC;
                break;
            }
	    break;

	case GET_STATIC:
	    /* Misnomer - this should actually take care of activation */
	    if (netcfg_get_static(client))
		state = GET_METHOD;
	    else
	        state = QUIT;
	    break;

        case WCONFIG:
	    if (requested_wireless_tools == 0)
	    {
	      di_exec_shell_log("apt-install wireless-tools");
	      requested_wireless_tools = 1;
	    }
	    state = WCONFIG_ESSID;
	    break;

	case WCONFIG_ESSID:
	    if (netcfg_wireless_set_essid (client, interface, NULL) == GO_BACK)
	      state = BACKUP;
	    else
	      state = WCONFIG_WEP;
	    break;
            
	case WCONFIG_WEP:
	    if (netcfg_wireless_set_wep (client, interface) == GO_BACK)
	      state = WCONFIG_ESSID;
	    else
	      state = GET_METHOD;
	    break;

	case QUIT:
	    return 0;
	}
    }

    return 0;
}

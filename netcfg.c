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

static enum {DHCP, STATIC} netcfg_method = DHCP;


int netcfg_get_method(struct debconfclient *client) 
{

    char *method;
    int ret;

    ret = my_debconf_input(client, "medium", "netcfg/get_method", &method);

    if (strcmp(method, "configure network with DHCP") == 0) 
	netcfg_method = DHCP;
    else 
	netcfg_method = STATIC;

    return ret;        
}


int main(int argc, char *argv[])
{
    int num_interfaces =0;
    enum { BACKUP, GET_INTERFACE, GET_METHOD, GET_DHCP, GET_STATIC, QUIT} state = GET_INTERFACE;
    static struct debconfclient *client;
    static char *none;



    /* initialize libd-i */
    di_system_init("netcfg");

    /* initialize debconf */
    client = debconfclient_new();
    debconf_capb(client, "backup");

    debconf_metaget(client,  "netcfg/internal-none", "description");
    none = client->value ? strdup(client->value) : strdup("<none>");

    while (state != QUIT) {

	switch(state) {
	case BACKUP:
	    exit(10);
	    break;
	case GET_INTERFACE:
	    state =  netcfg_get_interface(client, &interface, &num_interfaces) ?
		BACKUP : GET_METHOD;
	    break;
	case GET_METHOD:
	    if (netcfg_get_method(client))
		state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
	    else
		if (netcfg_method == DHCP) 
		    state = GET_DHCP;
		else
		    state = GET_STATIC;
	    break;
	case GET_DHCP:
	    if (netcfg_get_dhcp(client))
		state = GET_METHOD;
	    else {
		if (netcfg_activate_dhcp(client))
		    state = GET_STATIC;
		else
		    state = QUIT;
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
	case QUIT:
	    break;
	}
                
    }


    return 0;
}

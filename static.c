/* Static network configurator module for netcfg.
 *
 * Licensed under the terms of the GNU General Public License
 */

#include "netcfg.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <debian-installer.h>
#include <assert.h>

struct in_addr old_ipaddress = { 0 };
struct in_addr nameserver_array[4] = { { 0 }, };
struct in_addr network = { 0 };
struct in_addr broadcast = { 0 };
struct in_addr netmask = { 0 };
struct in_addr gateway = { 0 };
struct in_addr pointopoint = { 0 };

int netcfg_get_ipaddress(struct debconfclient *client)
{
    int ret, ok = 0;

    old_ipaddress = ipaddress;

    while (!ok)
    {
      debconf_input (client, "critical", "netcfg/get_ipaddress");
      ret = debconf_go (client);

      if (ret)
        return ret;

      debconf_get(client, "netcfg/get_ipaddress");
      ok = inet_pton (AF_INET, client->value, &ipaddress);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    return 0;
}

int netcfg_get_pointopoint(struct debconfclient *client)
{
    int ret, ok = 0;

    while (!ok)
    {
      debconf_input(client, "critical", "netcfg/get_pointopoint");
      ret = debconf_go(client);

      if (ret)
        return ret;

      debconf_get(client, "netcfg/get_pointopoint");
      
      if (empty_str(client->value)) /* No P-P is ok */
      {
	memset(&pointopoint, 0, sizeof(struct in_addr));
	return 0;
      }
      
      ok = inet_pton (AF_INET, client->value, &pointopoint);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    inet_pton (AF_INET, "255.255.255.255", &netmask);
    network = ipaddress;
    gateway = pointopoint;

    return 0;
}

int netcfg_get_netmask(struct debconfclient *client)
{
    int ret, ok = 0;
    char ptr1[INET_ADDRSTRLEN];
    struct in_addr old_netmask = netmask;
        
    while (!ok)
    {
      debconf_input (client, "critical", "netcfg/get_netmask");
      ret = debconf_go(client);

      if (ret)
        return ret;

      debconf_get (client, "netcfg/get_netmask");

      ok = inet_pton (AF_INET, client->value, &netmask);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    if (ipaddress.s_addr != old_ipaddress.s_addr ||
	netmask.s_addr != old_netmask.s_addr)
    {
      network.s_addr = ipaddress.s_addr & netmask.s_addr;
      broadcast.s_addr = (network.s_addr | ~netmask.s_addr);

      /* Preseed gateway */
      gateway.s_addr = ipaddress.s_addr & netmask.s_addr;
      gateway.s_addr |= htonl(1);
    }

    inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1));
    debconf_set(client, "netcfg/get_gateway", ptr1);

    return 0;
}
/* @brief Get the domainname.
 * @return 0 for success, with *domain = domain, 30 for 'goback',
 */
int netcfg_get_domain(struct debconfclient *client,  char **domain)
{
    int ret;
       
    if (have_domain == 1)
    {
      debconf_get(client, "netcfg/get_domain");
      assert (!empty_str(client->value));
      if (*domain)
	free(*domain);
      *domain = strdup(client->value);
      return 0;
    }

    debconf_input (client, "high", "netcfg/get_domain");
    ret = debconf_go(client);

    if (ret)
      return ret;
   
    debconf_get (client, "netcfg/get_domain");
    
    if (*domain)
        free(*domain);
    *domain = NULL;
    if (!empty_str(client->value))
        *domain = strdup(client->value);
    return 0;
}

int netcfg_get_gateway(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    while (!ok)
    {
      debconf_input (client, "critical", "netcfg/get_gateway");
      ret = debconf_go(client);

      if (ret)  
	return ret;

      debconf_get(client, "netcfg/get_gateway");
      ptr = client->value;

      if (empty_str(ptr)) /* No gateway, that's fine */
      {
	/* clear existing gateway setting */
	memset(&gateway, 0, sizeof(struct in_addr));
	return 0;
      }

      ok = inet_pton (AF_INET, ptr, &gateway);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    return 0;
}

int netcfg_get_nameservers (struct debconfclient *client, char **nameservers)
{
    char *ptr, ptr1[INET_ADDRSTRLEN];
    int ret;
       
    if (*nameservers)
        ptr = *nameservers;
    else if (gateway.s_addr)
    {
        inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1));
	ptr = ptr1;
    }
    else
	ptr = "";
    debconf_set(client, "netcfg/get_nameservers", ptr);
    
    debconf_input(client, "high", "netcfg/get_nameservers");
    ret = debconf_go(client);

    if (ret)
      return ret;

    debconf_get(client, "netcfg/get_nameservers");
    ptr = client->value;

    if (*nameservers)
        free(*nameservers);
    *nameservers = NULL;
    if (ptr)
        *nameservers = strdup(ptr);
    return ret;
}

void netcfg_nameservers_to_array(char *nameservers, struct in_addr array[])
{
    char *save, *ptr, *ns;
    int i;

    if (nameservers) {
        save = ptr = strdup(nameservers);

        for (i = 0; i < 3; i++)
        {
          ns = strtok_r(ptr, " \n\t", &ptr);
          if (ns)
            inet_pton (AF_INET, ns, &array[i]);
          else
            array[i].s_addr = 0;
        }

        array[3].s_addr = 0;
        free(save);
    } else
        array[0].s_addr = 0;
}

static int netcfg_write_static(char *prebaseconfig, char *domain,
			       struct in_addr nameservers[])
{
    char ptr1[INET_ADDRSTRLEN];
    FILE *fp;

    if ((fp = file_open(NETWORKS_FILE, "w"))) {
        fprintf(fp, "localnet %s\n", inet_ntop (AF_INET, &network, ptr1, sizeof (ptr1)));
        fclose(fp);
        
        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       NETWORKS_FILE,
                                       "/target" NETWORKS_FILE);
    } else
        goto error;

    if ((fp = file_open(INTERFACES_FILE, "a"))) {
        fprintf(fp,
                "\n# This entry was created during the Debian installation\n");
        fprintf(fp,
                "# (network, broadcast and gateway are optional)\n");
        if (!iface_is_hotpluggable(interface))
            fprintf(fp, "auto %s\n", interface);
        fprintf(fp, "iface %s inet static\n", interface);
        fprintf(fp, "\taddress %s\n", inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)));
        fprintf(fp, "\tnetmask %s\n", inet_ntop (AF_INET, &netmask, ptr1, sizeof (ptr1)));
        fprintf(fp, "\tnetwork %s\n", inet_ntop (AF_INET, &network, ptr1, sizeof (ptr1)));
        fprintf(fp, "\tbroadcast %s\n", inet_ntop (AF_INET, &broadcast, ptr1, sizeof (ptr1)));
        if (gateway.s_addr)
            fprintf(fp, "\tgateway %s\n", inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1)));
        if (pointopoint.s_addr)
            fprintf(fp, "\tpointopoint %s\n", inet_ntop (AF_INET, &pointopoint, ptr1, sizeof (ptr1)));
	if (is_wireless_iface(interface))
	{
	  fprintf(fp, "\twireless_mode %s\n",
	      (mode == MANAGED) ? "managed" : "ad-hoc");
	  fprintf(fp, "\twireless_essid %s\n", essid ? essid : "any");

	  if (wepkey != NULL)
	    fprintf(fp, "\twireless_key %s\n", wepkey);
	}
        fclose(fp);
    } else
        goto error;

    if ((fp = file_open(RESOLV_FILE, "w"))) {
        int i = 0;
        if (domain && !empty_str(domain))
            fprintf(fp, "search %s\n", domain);

        while (nameservers[i].s_addr)
            fprintf(fp, "nameserver %s\n",
                    inet_ntop (AF_INET, &nameservers[i++], ptr1, sizeof (ptr1)));

        fclose(fp);
    } else
	goto error;

    di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n", RESOLV_FILE,
				   "/target" RESOLV_FILE);

    return 0;
 error:
    return -1;
}

int netcfg_activate_static(struct debconfclient *client)
{
    int rv = 0;
    char buf[256], ptr1[INET_ADDRSTRLEN];

#ifdef __GNU__
    /* I had to do something like this ? */
    /*  di_exec_shell_log ("settrans /servers/socket/2 -fg");  */
    di_exec_shell_log("settrans /servers/socket/2 --goaway");
    snprintf(buf, sizeof(buf),
             "settrans -fg /servers/socket/2 /hurd/pfinet --interface=%s --address=%s",
             interface, inet_ntop (AF_INET, &ipaddress));
    di_snprintfcat(buf, sizeof(buf), " --netmask=%s",
                   inet_ntop (AF_INET, &netmask, ptr1, sizeof (ptr1)));
    buf[sizeof(buf) - 1] = '\0';

    if (gateway)
        snprintf(buf, sizeof(buf), " --gateway=%s",
                 inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1)));

    rv |= di_exec_shell_log(buf);

#else
    deconfigure_network();

    snprintf(buf, sizeof(buf), "ifconfig %s %s",
             interface, inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)));
    di_snprintfcat(buf, sizeof(buf), " netmask %s", inet_ntop (AF_INET, &netmask, ptr1, sizeof (ptr1)));
    di_snprintfcat(buf, sizeof(buf), " broadcast %s",
                   inet_ntop (AF_INET, &broadcast, ptr1, sizeof (ptr1)));
    buf[sizeof(buf) - 1] = '\0';

    if (pointopoint.s_addr)
        di_snprintfcat(buf, sizeof(buf), " pointopoint %s",
                       inet_ntop (AF_INET, &pointopoint, ptr1, sizeof (ptr1)));

    rv |= di_exec_shell_log(buf);

    if (gateway.s_addr) {
        snprintf(buf, sizeof(buf),
                 "route add default gateway %s",
                 inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1)));
        rv |= di_exec_shell_log(buf);
	printf("rv = %i, buf = %s\n", rv, buf);
    }
#endif

    if (rv != 0) {
        debconf_capb(client);
        debconf_input(client, "high", "netcfg/error");
        debconf_go(client);
        debconf_capb(client, "backup");
        return -1;
    }

    /* write configuration */

    netcfg_write_common("40netcfg", ipaddress, hostname, domain);
    netcfg_write_static("40netcfg", domain, nameserver_array);

    return 0;
}

int netcfg_get_static(struct debconfclient *client) 
{
    char *nameservers = NULL;
    char ptr1[INET_ADDRSTRLEN];
    char *none;

    enum { BACKUP, GET_IPADDRESS, GET_POINTOPOINT, GET_NETMASK, GET_GATEWAY, 
           GATEWAY_UNREACHABLE, GET_NAMESERVERS, CONFIRM, GET_DOMAIN, QUIT } state = GET_IPADDRESS;

    ipaddress.s_addr = network.s_addr = broadcast.s_addr = netmask.s_addr = gateway.s_addr = pointopoint.s_addr =
        0;

    debconf_metaget(client,  "netcfg/internal-none", "description");
    none = client->value ? strdup(client->value) : strdup("<none>");

    while (state != QUIT) {
        switch (state) {
        case BACKUP:
            return 10; /* Back to main */
            break;
        case GET_IPADDRESS:
            if (netcfg_get_ipaddress (client)) {
                state = BACKUP;
            } else {
                if (strncmp(interface, "plip", 4) == 0
                    || strncmp(interface, "slip", 4) == 0
                    || strncmp(interface, "ctc", 3) == 0
                    || strncmp(interface, "escon", 5) == 0)
                    state = GET_POINTOPOINT;
                else
                    state = GET_NETMASK;
            }
            break;
	
        case GET_POINTOPOINT:
            state = netcfg_get_pointopoint(client) ?
                GET_IPADDRESS : GET_NAMESERVERS;
            break;
	
        case GET_NETMASK:
            state = netcfg_get_netmask(client) ?
                GET_IPADDRESS : GET_GATEWAY;
            break;
	
        case GET_GATEWAY:
            if (netcfg_get_gateway(client))
                state = GET_NETMASK;
            else 
                if (gateway.s_addr && ((gateway.s_addr & netmask.s_addr) != network.s_addr))
                    state = GATEWAY_UNREACHABLE;
                else
                    state = GET_NAMESERVERS;
            break;
        case GATEWAY_UNREACHABLE:
            debconf_capb(client); /* Turn off backup */
            debconf_input(client, "high", "netcfg/gateway_unreachable");
            debconf_go(client);
            state = GET_GATEWAY;
            debconf_capb(client, "backup");
            break;
        case GET_NAMESERVERS:
            state = (netcfg_get_nameservers (client, &nameservers)) ?
                GET_GATEWAY : GET_DOMAIN;
            break;
        case GET_DOMAIN:
            state = (netcfg_get_domain (client, &domain)) ?
                GET_NAMESERVERS : CONFIRM;
            break;
        case CONFIRM:
            debconf_subst(client, "netcfg/confirm_static", "interface", interface);
            debconf_subst(client, "netcfg/confirm_static", "ipaddress",
                          (ipaddress.s_addr ? inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)) : none));
            debconf_subst(client, "netcfg/confirm_static", "pointopoint",
                          (pointopoint.s_addr ? inet_ntop (AF_INET, &pointopoint, ptr1, sizeof (ptr1)) : none));
            debconf_subst(client, "netcfg/confirm_static", "netmask",
                          (netmask.s_addr ? inet_ntop (AF_INET, &netmask, ptr1, sizeof (ptr1)) : none));
            debconf_subst(client, "netcfg/confirm_static", "gateway",
                          (gateway.s_addr ? inet_ntop (AF_INET, &gateway, ptr1, sizeof (ptr1)) : none));
            debconf_subst(client, "netcfg/confirm_static", "hostname", hostname);
            debconf_subst(client, "netcfg/confirm_static", "domain",
                          (domain ? domain : none));
            debconf_subst(client, "netcfg/confirm_static", "nameservers",
                          (nameservers ? nameservers : none));
            netcfg_nameservers_to_array(nameservers, nameserver_array);

            debconf_capb(client); /* Turn off backup for yes/no confirmation */

            debconf_input(client, "medium", "netcfg/confirm_static");
            debconf_go(client);
            debconf_get(client, "netcfg/confirm_static");
            state = strstr(client->value, "true") ? QUIT : GET_IPADDRESS;
            debconf_capb(client, "backup");
            break;
        case QUIT:
            return 0;
            break;
        }
    }
    return 0;
}


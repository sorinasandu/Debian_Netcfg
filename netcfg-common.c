/* 
   netcfg-common.c - Shared functions used to configure the network for 
   the debian-installer.

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

#define _GNU_SOURCE

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define empty_str(s) (s && *s == '\0')

#include <assert.h>
#include <ctype.h>
#include <iwlib.h>
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
#include <time.h>
#include "netcfg.h"


/* Set if there is currently a progress bar displayed. */
int netcfg_progress_displayed = 0;

/* Set if DCHP client exits */
volatile int dhcp_running = 0; /* not running */
volatile int dhcp_exit_status = -1; /* failed */

/* network config */
char *interface = NULL;
char *hostname = NULL;
char *dhcp_hostname = NULL;
char *domain = NULL;
u_int32_t ipaddress = 0;
u_int32_t nameserver_array[4] = { 0 };
u_int32_t network = 0;
u_int32_t broadcast = 0;
u_int32_t netmask = 0;
u_int32_t gateway = 0;
u_int32_t pointopoint = 0;

/* wireless config */
char* wepkey = NULL;
char* essid = NULL;

/* IW socket for global use - init in main */
int wfd = 0;

int my_debconf_input(struct debconfclient *client, char *priority,
                     char *template, char **p)
{
    int ret = 0;
    debconf_fset(client, template, "seen", "false");
    debconf_input(client, priority, template);
    ret = debconf_go(client);
    debconf_get(client, template);
    *p = client->value;
    return ret;
}

/* Signal handler for DHCP client child */
static void dhcp_client_sigchld(int sig) 
{
    (void)sig;
    if (dhcp_running == 1) {
	dhcp_running = 0;
	wait(&dhcp_exit_status);
    } 
}

int is_interface_up(char *inter)
{
    struct ifreq ifr;
    int sfd = -1, ret = -1;
    
    if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        goto int_up_done;
    
    strncpy(ifr.ifr_name, inter, sizeof(ifr.ifr_name));
    
    if (ioctl(sfd, SIOCGIFFLAGS, &ifr) < 0)
        goto int_up_done;

    ret = (ifr.ifr_flags & IFF_UP) ? 1 : 0;
    
 int_up_done:
    if (sfd != -1)
        close(sfd);
    return ret;
}

void get_name(char *name, char *p)
{
    while (isspace(*p))
        p++;
    while (*p) {
        if (isspace(*p))
            break;
        if (*p == ':') {        /* could be an alias */
            char *dot = p, *dotname = name;
            *name++ = *p++;
            while (isdigit(*p))
                *name++ = *p++;
            if (*p != ':') {        /* it wasn't, backup */
                p = dot;
                name = dotname;
            }
            if (*p == '\0')
                return;
            p++;
            break;
        }
        *name++ = *p++;
    }
    *name++ = '\0';
    return;
}


static FILE *ifs = NULL;
static char ibuf[512];

void getif_start(void)
{
    if (ifs != NULL) {
        fclose(ifs);
        ifs = NULL;
    }
    if ((ifs = fopen("/proc/net/dev", "r")) != NULL) {
        fgets(ibuf, sizeof(ibuf), ifs); /* eat header */
        fgets(ibuf, sizeof(ibuf), ifs); /* ditto */
    }
    return;
}


char *getif(int all)
{
    char rbuf[512];
    if (ifs == NULL)
        return NULL;

    if (fgets(rbuf, sizeof(rbuf), ifs) != NULL) {
        get_name(ibuf, rbuf);
        if (!strcmp(ibuf, "lo"))        /* ignore the loopback */
            return getif(all);      /* seriously doubt there is an infinite number of lo devices */
        if (all || is_interface_up(ibuf) == 1)
            return ibuf;
    }
    return NULL;
}


void getif_end(void)
{
    if (ifs != NULL) {
        fclose(ifs);
        ifs = NULL;
    }
    return;
}


char *get_ifdsc(struct debconfclient *client, const char *ifp)
{
    char template[256];

    if (strlen(ifp) < 100) {
      if (!is_wireless_iface(ifp))
      {
        /* strip away the number from the interface (eth0 -> eth) */
        char *new_ifp = strdup(ifp), *ptr = new_ifp;
        while ((*ptr < '0' || *ptr > '9') && *ptr != '\0')
            ptr++;
        *ptr = '\0';

        sprintf(template, "netcfg/internal-%s", new_ifp);
        free(new_ifp);

        debconf_metaget(client, template, "description");
        if (client->value != NULL)
            return strdup(client->value);
      }
      else
      {
	strcpy(template, "netcfg/internal-wifi");
	debconf_metaget(client, template, "description");
	return strdup(client->value);
      }
    }
    debconf_metaget(client, "netcfg/internal-unknown-iface", "description");
    if (client->value != NULL)
        return strdup(client->value);
    else
        return strdup("Unknown interface");
}


FILE *file_open(char *path, const char *opentype)
{
    FILE *fp;
    if ((fp = fopen(path, opentype)))
        return fp;
    else {
        fprintf(stderr, "%s\n", path);
        perror("fopen");
        return NULL;
    }
}


void dot2num(u_int32_t * num, char *dot)
{
    char *p = dot - 1;
    char *e;
    int ix;
    unsigned long val;

    if (!dot)
        goto exit;

    *num = 0;
    for (ix = 0; ix < 4; ix++) {
        *num <<= 8;
        p++;
        val = strtoul(p, &e, 10);
        if (e == p)
            val = 0;
        else if (val > 255)
            goto exit;
        *num += val;
        /*printf("%#8x, %#2x\n", *num, val); */
        if (ix < 3 && *e != '.')
            goto exit;
        p = e;
    }

    return;

 exit:
    *num = 0;
}


static char num2dot_buf[16];

char *num2dot(u_int32_t num)
{
    int byte[4];
    int ix;
    char *dot = num2dot_buf;

    for (ix = 3; ix >= 0; ix--) {
        byte[ix] = num & 0xff;
        num >>= 8;
    }
    sprintf(dot, "%d.%d.%d.%d", byte[0], byte[1], byte[2], byte[3]);

    return dot;
}



void netcfg_die(struct debconfclient *client)
{
    if (netcfg_progress_displayed)
        debconf_progress_stop(client);
    debconf_capb(client);
    debconf_input(client, "high", "netcfg/error");
    debconf_go(client);
    exit(1);
}

/**
 * @brief Ask which interface to configure
 * @param client - client 
 * @param interface      - set to the answer
 * @param numif - number of interfaces found.
 */

int netcfg_get_interface(struct debconfclient *client, char **interface,
                         int *numif)
{
    char *inter;
    size_t len;
    int ret;
    int num_interfaces = 0;
    char *ptr = NULL;
    char *ifdsc;

    if (*interface) {
        free(*interface);
        *interface = NULL;
    }

    if (!(ptr = malloc(128)))
	goto error;

    len = 128;
    *ptr = '\0';

    getif_start();
    while ((inter = getif(1)) != NULL) {
	size_t newchars;

	ifdsc = get_ifdsc(client, inter);
        newchars = strlen(inter) + strlen(ifdsc) + 5;
        if (len < (strlen(ptr) + newchars)) {
            if (!(ptr = realloc(ptr, len + newchars + 128)))
                goto error;
            len += newchars + 128;
        }
        di_snprintfcat(ptr, len, "%s: %s, ", inter, ifdsc);
        num_interfaces++;
        free(ifdsc);
    }
    getif_end();

    if (num_interfaces == 0) {
        debconf_input(client, "high", "netcfg/no_interfaces");
        debconf_go(client);
        free(ptr);
        exit(1);
    } else if (num_interfaces > 1) {
        *numif = num_interfaces;
        /* remove the trailing ", ", which confuses cdebconf */
        ptr[strlen(ptr) - 2] = '\0';

        debconf_subst(client, "netcfg/choose_interface", "ifchoices", ptr);
        free(ptr);
        ret = my_debconf_input(client, "high",
                               "netcfg/choose_interface", &inter);

        if (ret)
            return ret;
        if (!inter)
            netcfg_die(client);
    } else if (num_interfaces == 1) {
        inter = ptr;
        *numif = 1;
    }

    /* grab just the interface name, not the description too */
    *interface = inter;
    ptr = strchr(inter, ':');
    if (ptr == NULL)
        goto error;
    *ptr = '\0';

    *interface = strdup(*interface);

    return 0;

 error:
    if (ptr)
        free(ptr);

    netcfg_die(client);
    return 10; /* unreachable */
}

/*
 * Set the hostname. 
 * @return 0 on success, 30 on BACKUP being selected.
 */
int netcfg_get_hostname(struct debconfclient *client, char **hostname)
{
    static const char *valid_chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    size_t len;
    int ret;
    char *p;

    do {

        ret = my_debconf_input(client, "high", "netcfg/get_hostname", &p);
        if (ret == 30) // backup
            return ret;
        free(*hostname);
        *hostname = strdup(p);
        len = strlen(*hostname);

        /* Check the hostname for RFC 1123 compliance.  */
        if ((len < 2) ||
            (len > 63) ||
            (strspn(*hostname, valid_chars) != len) ||
            ((*hostname)[len - 1] == '-') ||
            ((*hostname)[0] == '-')) {
            debconf_subst(client, "netcfg/invalid_hostname",
                          "hostname", *hostname);
            debconf_input(client, "high", "netcfg/invalid_hostname");
            debconf_go(client);
            free(*hostname);
            *hostname = NULL;
            debconf_set(client, "netcfg/get_hostname", "debian");
        }
        
    } while (!*hostname);
    return 0;
}

/* @brief Get the domainname.
 * @return 0 for success, with *domain = domain, 30 for 'goback',
 */
int netcfg_get_domain(struct debconfclient *client,  char **domain)
{
    int ret;
    char *ptr;
        
    ret = my_debconf_input(client, "high", "netcfg/get_domain", &ptr);
    if (ret)
        return ret;
    if (*domain)
        free(*domain);
    *domain = NULL;
    if (ptr && ptr[0])
        *domain = strdup(ptr);
    return 0;
}


void netcfg_write_common(const char *prebaseconfig, u_int32_t ipaddress,
			 char *hostname, char *domain)
{
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "w"))) {
        fprintf(fp, "auto lo\n");
        fprintf(fp, "iface lo inet loopback\n");
        fclose(fp);

        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       INTERFACES_FILE,
                                       "/target" INTERFACES_FILE);
    }

    /* Currently busybox, hostname is not available. */
    if ((fp = file_open("/proc/sys/kernel/hostname", "w"))) {
        fprintf(fp, "%s\n", hostname);
        fclose(fp);
    }

    if ((fp = file_open(HOSTNAME_FILE, "w"))) {
       fprintf(fp, "%s\n", hostname);
       fclose(fp);
        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       HOSTNAME_FILE, "/target" HOSTNAME_FILE);
    }

    if ((fp = file_open(HOSTS_FILE, "w"))) {
        if (ipaddress) {
            fprintf(fp, "127.0.0.1\tlocalhost\n");
            if (!empty_str(domain))
                fprintf(fp, "%s\t%s.%s\t%s\n",
                        num2dot(ipaddress), hostname,
                        domain, hostname);
            else
                fprintf(fp, "%s\t%s\n", num2dot(ipaddress),
                        hostname);
        } else {
            fprintf(fp, "127.0.0.1\t%s\tlocalhost\n", hostname);
        }

        fclose(fp);

        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       HOSTS_FILE, "/target" HOSTS_FILE);
    }
}

int is_valid_ip (char* ipaddr)
{
  int ok = 1, nums = 0;
  
  if (!empty_str(ipaddr))
  {
    char* ptr = strdup(ipaddr), *tok;
    tok = strtok(ptr, ".");

    while (tok)
    {
      int spaz;

      spaz = atoi(tok);
      if (spaz >= 0 && spaz <= 255)
      {
	ok = 1;
	nums++;
      }
      else
      {
	ok = 0;
	break;
      }

      tok = strtok(NULL, ".");
    }
  }

  if (nums != 4)
    ok = 0;
  
  return ok;
}

int netcfg_get_ipaddress(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_ipaddress", &ptr);
      if (ret)
	return ret;

      ok = is_valid_ip (ptr);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    dot2num(&ipaddress, ptr);
    return 0;
}

int netcfg_get_pointopoint(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_pointopoint", &ptr);
      if (ret)  
	return ret;

      ok = is_valid_ip(ptr);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    dot2num(&pointopoint, ptr);
    dot2num(&netmask, "255.255.255.255");
    network = ipaddress;
    gateway = pointopoint;

    return 0;
}

int netcfg_get_netmask(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;
        
    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_netmask", &ptr);

      if (ret)
	return ret;

      ok = is_valid_ip(ptr);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    dot2num(&netmask, ptr);
    network = ipaddress & netmask;
    broadcast = (network | ~netmask);

    /* Preseed gateway */
    gateway = ipaddress & netmask;
    debconf_set(client, "netcfg/get_gateway", num2dot(gateway+1));

    return 0;
}

int netcfg_get_gateway(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    while (!ok)
    {
      ret = my_debconf_input(client, "critical", "netcfg/get_gateway", &ptr);
      if (ret)  
	return ret;

      ok = is_valid_ip(ptr);
      
      if (!ok)
      {
	debconf_input (client, "critical", "netcfg/bad_ipaddress");
	debconf_go (client);
      }
    }

    dot2num(&gateway, ptr);

    return 0;
}


int netcfg_get_nameservers (struct debconfclient *client, char **nameservers)
{
    char *ptr;
    int ret;
       
    debconf_set(client, "netcfg/get_nameservers", (gateway ? num2dot(gateway) :  ""));
    
    ret = my_debconf_input(client, "high", "netcfg/get_nameservers", &ptr);
    if (*nameservers)
        free(*nameservers);
    *nameservers = NULL;
    if (ptr)
        *nameservers = strdup(ptr);
    return ret;
}

void netcfg_nameservers_to_array(char *nameservers, u_int32_t array[])
{

    char *save, *ptr, *ns;

    if (nameservers) {
        save = ptr = strdup(nameservers);

        ns = strtok_r(ptr, " \n\t", &ptr);
        dot2num(&array[0], ns);

        ns = strtok_r(NULL, " \n\t", &ptr);
        dot2num(&array[1], ns);

        ns = strtok_r(NULL, " \n\t", &ptr);
        dot2num(&array[2], ns);

        array[3] = 0;
        free(save);
    } else
        array[0] = 0;
}

static int netcfg_write_static(char *prebaseconfig, char *domain,
			       u_int32_t nameservers[])
{
    FILE *fp;

    if ((fp = file_open(NETWORKS_FILE, "w"))) {
        fprintf(fp, "localnet %s\n", num2dot(network));
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
	if (is_wireless_iface(interface))
	{
	  if (essid != NULL)
	    fprintf(fp, "\twireless_essid %s\n", essid);
	  if (wepkey != NULL)
	    fprintf(fp, "\twireless_key %s\n", wepkey);
	}
        fclose(fp);
    } else
        goto error;

    if ((fp = file_open(RESOLV_FILE, "w"))) {
        int i = 0;
        if (!empty_str(domain))
            fprintf(fp, "search %s\n", domain);

        while (nameservers[i])
            fprintf(fp, "nameserver %s\n",
                    num2dot(nameservers[i++]));

        fclose(fp);
    } else
	goto error;

    di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n", RESOLV_FILE,
				   "/target" RESOLV_FILE);

    return 0;
 error:
    return -1;
}

int kill_dhcp_client(void) {
    FILE *ps;
    char *pid_char = NULL;
    int pid_int = 0;
    size_t linesize = 0;
    ssize_t ret = 0;

    /* kill running dhcp client */

    if ((ps = popen("ps xa | grep 'udhcpc\\|dhclient\\|pump' | grep -v grep | cut -d ' ' -f 2", "r")) == NULL)
        return 1;

    ret = getline(&pid_char, &linesize, ps);
    if (ret < 1)
        return 2;
  
    pclose(ps);

    pid_int = atoi(pid_char);

    if (kill(pid_int, SIGTERM) != 0)
        return 3;

    sleep(2);

    if (kill(pid_int, SIGTERM) == 0)
        kill(pid_int, SIGKILL);
    else 
        return 0;

    sleep(2);

    if (kill(pid_int, SIGTERM) == 0)
        return 4;

    return 0;

}

int deconfigure_network(void) {

    char buf[256];

    kill_dhcp_client();
    
    /* deconfiguring network interfaces */
    di_exec_shell_log("ifconfig lo down");
    snprintf(buf, sizeof(buf), "ifconfig %s down", interface);
    di_exec_shell_log(buf);

    return 0;
}

int netcfg_activate_static(struct debconfclient *client)
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
    deconfigure_network();

    /* configure loopback */
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
    char *ptr;
    char *none;

    enum { BACKUP, GET_IPADDRESS, GET_POINTOPOINT, GET_NETMASK, GET_GATEWAY, 
           GATEWAY_UNREACHABLE, GET_NAMESERVERS, GET_HOSTNAME, CONFIRM, 
           GET_DOMAIN, QUIT} state = GET_IPADDRESS;

    ipaddress = network = broadcast = netmask = gateway = pointopoint =
        0;

    debconf_metaget(client,  "netcfg/internal-none", "description");
    none = client->value ? strdup(client->value) : strdup("<none>");

    while (state != QUIT) {
        switch (state) {
        case BACKUP:
            return 10; // Back to main
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
                if (gateway && ((gateway & netmask) != network))
                    state = GATEWAY_UNREACHABLE;
                else
                    state = GET_NAMESERVERS;
            break;
        case GATEWAY_UNREACHABLE:
            debconf_capb(client); // Turn off backup
            debconf_input(client, "high", "netcfg/gateway_unreachable");
            debconf_go(client);
            state = GET_GATEWAY;
            debconf_capb(client, "backup");
            break;
        case GET_NAMESERVERS:
            state = (netcfg_get_nameservers (client, &nameservers)) ?
                GET_GATEWAY : GET_HOSTNAME;
            break;
        case GET_HOSTNAME:
            state = (netcfg_get_hostname(client, &hostname)) ?
                GET_NAMESERVERS : GET_DOMAIN;
            break;
        case GET_DOMAIN:
            state = (netcfg_get_domain (client, &domain)) ?
                GET_HOSTNAME : CONFIRM;
            break;
        case CONFIRM:
            debconf_subst(client, "netcfg/confirm_static", "interface", interface);
            debconf_subst(client, "netcfg/confirm_static", "ipaddress",
                          (ipaddress ? num2dot(ipaddress) : none));
            debconf_subst(client, "netcfg/confirm_static", "pointopoint",
                          (pointopoint ? num2dot(pointopoint) : none));
            debconf_subst(client, "netcfg/confirm_static", "netmask",
                          (netmask ? num2dot(netmask) : none));
            debconf_subst(client, "netcfg/confirm_static", "gateway",
                          (gateway ? num2dot(gateway) : none));
            debconf_subst(client, "netcfg/confirm_static", "hostname", hostname);
            debconf_subst(client, "netcfg/confirm_static", "domain",
                          (domain ? domain : none));
            debconf_subst(client, "netcfg/confirm_static", "nameservers",
                          (nameservers ? nameservers : none));
            netcfg_nameservers_to_array(nameservers, nameserver_array);

            debconf_capb(client); // Turn off backup for yes/no confirmation
            my_debconf_input(client, "medium", "netcfg/confirm_static", &ptr);
            state = strstr(ptr, "true") ? QUIT : GET_IPADDRESS;
            debconf_capb(client, "backup");
            break;
        case QUIT:
            return 0;
            break;
        }
    }
    return 0;
}


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


static void netcfg_write_dhcp(char *iface, char *host)
{
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "a"))) {
        fprintf(fp,
                "\n# This entry was created during the Debian installation\n");
        fprintf(fp, "auto %s\n", iface);
        fprintf(fp, "iface %s inet dhcp\n", iface);
        if (host)
            fprintf(fp, "\thostname %s\n", host);
	if (is_wireless_iface(iface))
	{
	  if (essid)
	    fprintf(fp, "\twireless_essid %s", essid);
	  if (wepkey)
	    fprintf(fp, "\twireless_key %s", wepkey);
	}
        fclose(fp);
    }
}


int netcfg_activate_dhcp(struct debconfclient *client)
{
    char buf[128];
    struct stat stat_buf;
    time_t start_time, now;
    pid_t pid = 0;
    int retry = 1;
    char *ptr;

    enum { PUMP, DHCLIENT, UDHCPC } dhcp_client;

    if (stat("/sbin/dhclient", &stat_buf) == 0)
        dhcp_client = DHCLIENT;
    else if (stat("/sbin/udhcpc", &stat_buf) == 0)
        dhcp_client = UDHCPC;
    else if (stat("/sbin/pump", &stat_buf) == 0)
        dhcp_client = PUMP;
    else {
        debconf_input(client, "critical", "netcfg/no_dhcp_client");
        debconf_go(client);
        exit(1);
    }

    deconfigure_network();

    /* setup loopback */
    di_exec_shell_log("/sbin/ifconfig lo 127.0.0.1");

    /* load kernel module for network sockets */
    di_exec_shell_log("/sbin/modprobe af_packet");

    /* get dhcp lease */
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

    while (retry == 1) {
        /* show progress bar */
        debconf_progress_start(client, 0, 10, "netcfg/dhcp_progress");
        debconf_progress_info(client, "netcfg/dhcp_progress_note");
        netcfg_progress_displayed = 1;

        now = start_time = time(NULL);
        if (! (dhcp_running || (dhcp_exit_status == 0))) {
            if ((pid = fork()) == 0) {
                int ret = di_exec_shell_log(buf);
                ((WIFEXITED(ret) && (WEXITSTATUS(ret) != 0)) || WIFSIGNALED(ret)) ?
                    _exit(EXIT_FAILURE) : _exit(EXIT_SUCCESS);
            }
            if (pid)
                dhcp_running = 1;
            else
                return 1;
            signal(SIGCHLD, &dhcp_client_sigchld);
        }

        /* wait 10s for a DHCP lease */
        while (dhcp_running && ((now - start_time) < 10)) {
            sleep(1);
            debconf_progress_step(client, 1);
            now = time(NULL);
        }

        /* stop progress bar */
        debconf_progress_stop(client);
        netcfg_progress_displayed = 0;

        /* got a lease? */
        if (!dhcp_running && (dhcp_exit_status == 0)) {
	    /* even systems on dhcp get hostnames */
	    /* TODO use the dhcp hostname if available. */
            if (netcfg_get_hostname(client, &hostname))
                return 30; // backup
	    
            /* write configuration */
            netcfg_write_common("40netcfg", ipaddress, hostname, domain);
            netcfg_write_dhcp(interface, dhcp_hostname);

            return 0;
        }

        /* ask if user wants to retry */
        if (my_debconf_input(client, "high", "netcfg/dhcp_retry", &ptr) == 30) {
            if (dhcp_running) {
                kill(pid, SIGTERM);
            }
	    return 30; // backup
	}
        retry = strstr(ptr, "true") ? 1 : 0;
    }

    if (dhcp_running) {
        kill(pid, SIGTERM);
    }
    return 1;
}

int netcfg_get_dhcp(struct debconfclient *client) {

    enum { BACKUP, GET_DHCP_HOSTNAME, QUIT } state = GET_DHCP_HOSTNAME;

    while (1) {
        switch (state) {
        case BACKUP:
            return 10; // Back to main
        case GET_DHCP_HOSTNAME:
            state = netcfg_get_dhcp_hostname(client, &dhcp_hostname) ?
                BACKUP : QUIT;
            break;
        case QUIT:
            return 0;
        }
    }
    return 0;
}

int is_wireless_iface (const char* iface)
{
  wireless_config wc;

  if (wfd == 0)
    wfd = iw_sockets_open();
  
  return (iw_get_basic_config (wfd, (char*)iface, &wc) == 0);
}

int netcfg_wireless_set_essid (struct debconfclient * client, char *iface)
{
  enum { ADHOC, MANAGED } mode = MANAGED;
  int ret;
  char* tf = NULL;
  wireless_config wconf;
  
  if (wfd == 0) /* shouldn't happen */
    wfd = iw_sockets_open();

  iw_get_basic_config (wfd, iface, &wconf);

  debconf_subst(client, "netcfg/wireless_essid", "iface", iface);
  debconf_subst(client, "netcfg/wireless_adhoc_managed", "iface", iface);

  ret = my_debconf_input(client, "low", "netcfg/wireless_adhoc_managed", &tf);

  if (ret == 30)
    return ret;

  if (!strcmp(tf, "Ad-hoc network (Peer to peer)"))
    mode = ADHOC;

  tf = NULL;
  
  ret = my_debconf_input(client, "low", "netcfg/wireless_essid", &tf);

  if (ret == 30)
    return ret;
  
  /* question not asked or user doesn't care or we're successfully associated */
  if (!empty_str(wconf.essid) || empty_str(tf)) 
  {
    /* Default to mode managed, AP any */
    wconf.essid[0] = '\0';
    wconf.essid_on = 0;
    wconf.has_mode = 1;
    wconf.mode = 2; /* MANAGED */

    iw_set_basic_config (wfd, iface, &wconf);
    
    return 0;
  }
  /* yes, wants to set an essid by himself */
  else
  {
    char* user_essid = NULL, *ptr = wconf.essid;

    if (strlen(tf) <= IW_ESSID_MAX_SIZE) /* looks ok, let's use it */
      user_essid = tf;
    
    while (!user_essid || empty_str(user_essid) ||
	strlen(user_essid) > IW_ESSID_MAX_SIZE)
    {
      debconf_subst(client, "netcfg/invalid_essid", "essid", user_essid);
      debconf_input(client, "high", "netcfg/invalid_essid");
      debconf_go(client);
      
      ret = my_debconf_input(client, "low", "netcfg/wireless_essid", &user_essid);
      assert (ret != 30);
    }

    essid = strdup (user_essid);

    memset(ptr, 0, IW_ESSID_MAX_SIZE + 1);
    snprintf(wconf.essid, IW_ESSID_MAX_SIZE + 1, "%s", essid);
    wconf.has_essid = 1;
    wconf.essid_on = 1;

    iw_set_basic_config (wfd, iface, &wconf);
  }

  return 0;
}

void unset_wep_key (char* iface)
{
  wireless_config wconf;
  int ret;

  if (!wfd)
    wfd = iw_sockets_open();

  iw_get_basic_config(wfd, iface, &wconf);

  wconf.has_key = 1;
  wconf.key[0] = '\0';
  wconf.key_flags = IW_ENCODE_DISABLED | IW_ENCODE_NOKEY;
  wconf.key_size = 0;

  ret = iw_set_basic_config (wfd, iface, &wconf);

  fprintf (stderr, "iw_set_basic_config for uncode returned %d",
      ret);
}

int netcfg_wireless_set_wep (struct debconfclient * client, char* iface)
{
  wireless_config wconf;
  char* rv = NULL;
  int ret, keylen;
  unsigned char buf [IW_ENCODING_TOKEN_MAX];
  
  iw_get_basic_config (wfd, iface, &wconf);

  debconf_subst(client, "netcfg/wireless_wep", "iface", iface);
  ret = my_debconf_input (client, "high", "netcfg/wireless_wep", &rv);

  if (ret == 30)
    return ret;

  if (empty_str(rv))
  {
    di_log(DI_LOG_LEVEL_DEBUG, "unsetting WEP key for device %s", iface);
    unset_wep_key (iface);

    if (wepkey != NULL)
    {
      free(wepkey);
      wepkey = NULL;
    }
    
    return 0;
  }

  di_log(DI_LOG_LEVEL_DEBUG, "proceeding to set WEP key for device %s (%s)",
      iface, rv);

  while ((keylen = iw_in_key (rv, buf)) == -1)
  {
    debconf_subst(client, "netcfg/invalid_wep", "wepkey", rv);
    debconf_input(client, "high", "netcfg/invalid_wep");
    debconf_go(client);
    
    ret = my_debconf_input (client, "high", "netcfg/wireless_wep", &rv);
  }

  wconf.has_key = 1;
  wconf.key_size = keylen;
  wconf.key_flags = IW_ENCODE_ENABLED | IW_ENCODE_OPEN;
  
  strncpy (wconf.key, buf, keylen);

  wepkey = strdup(rv);

  iw_set_basic_config (wfd, iface, &wconf);

  return 0;
}

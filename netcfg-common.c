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
char *domain = NULL;
struct in_addr ipaddress = { 0 };
struct in_addr old_ipaddress = { 0 };
struct in_addr nameserver_array[4] = { { 0 }, };
struct in_addr network = { 0 };
struct in_addr broadcast = { 0 };
struct in_addr netmask = { 0 };
struct in_addr gateway = { 0 };
struct in_addr pointopoint = { 0 };

/* Wireless mode */
enum { ADHOC, MANAGED } mode = MANAGED;

/* wireless config */
char* wepkey = NULL;
char* essid = NULL;

/* IW socket for global use - init in main */
int wfd = 0;

static int have_domain = 0;

int my_debconf_input(struct debconfclient *client, char *priority,
                     char *template, char **p)
{
    int ret = 0;
    debconf_input(client, priority, template);
    ret = debconf_go(client);
    debconf_get(client, template);
    *p = client->value;
    return ret;
}

/* Signal handler for DHCP client child */
static void dhcp_client_sigchld(int sig __attribute__ ((unused))) 
{
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

char *find_in_devnames(const char* iface)
{
#define DEVNAMES "/etc/network/devnames"
  FILE* dn = NULL;
  char buf[512], *result = NULL;
  size_t len = strlen(iface);

  if (!(dn = fopen(DEVNAMES, "r")))
    return NULL;

  while (fgets(buf, 512, dn) != NULL)
  {
    char *ptr = strchr(buf, ':'), *desc = ptr + 1;

    if (!ptr)
    {
      result = NULL; /* corrupt */
      break;
    }
    else if (!strncmp(buf, iface, len))
    {
      result = strdup(desc);
      break;
    }
  }

  fclose(dn);

  len = strlen(result);
  
  if (result[len - 1] == '\n')
    result[len - 1] = '\0';

  return result;
}

char *get_ifdsc(struct debconfclient *client, const char *ifp)
{
    char template[256], *ptr = NULL;

    if ((ptr = find_in_devnames(ifp)) != NULL)
      return ptr; /* already strdup'd */
    
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

int iface_is_hotpluggable(const char *iface)
{
#define DEVHOTPLUG "/etc/network/devhotplug"
    FILE* f = NULL;
    char buf[256];
    size_t len = strlen(iface);
    int result = 0;
    
    if (!(f = fopen(DEVHOTPLUG, "r")))
        return 0;
    
    while (fgets(buf, 256, f) != NULL)
    {
        if (!strncmp(buf, iface, len))
        {
            result = 1;
            break;
        }
    }
    
    fclose(f);
    
    return result;
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

	ifconfig_down(inter);
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
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
    size_t len;
    int ret;
    char *p, *s;

    do {
	have_domain = 0;
        ret = my_debconf_input(client, "high", "netcfg/get_hostname", &p);
        if (ret == 30) /* backup */
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

    if ((s = strchr(*hostname, '.')))
    {
      if (s[1] == '\0') /* "somehostname." <- . should be ignored */
	*s = '\0';
      else /* assume we have a valid domain name here */
      {
	have_domain = 1;
	debconf_set(client, "netcfg/get_domain", strdup(s + 1));
      }
    }
    return 0;
}

/* @brief Get the domainname.
 * @return 0 for success, with *domain = domain, 30 for 'goback',
 */
int netcfg_get_domain(struct debconfclient *client,  char **domain)
{
    int ret;
    char *ptr;
       
    if (have_domain == 1)
    {
      debconf_get(client, "netcfg/get_domain");
      assert (!empty_str(client->value));
      if (*domain)
	free(*domain);
      *domain = strdup(client->value);
      return 0;
    }
    
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

#define HELPFUL_COMMENT \
"# This file describes the network interfaces available on your system\n" \
"# and how to activate them. For more information, see interfaces(5).\n" \
"\n" \
"# This entry denotes the loopback (127.0.0.1) interface.\n"

void netcfg_write_common(const char *prebaseconfig, struct in_addr ipaddress,
			 char *hostname, char *domain)
{
    char ptr1[INET_ADDRSTRLEN];
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "w"))) {
        fprintf(fp, HELPFUL_COMMENT);
        fprintf(fp, "auto lo\n");
        fprintf(fp, "iface lo inet loopback\n");
        if (iface_is_hotpluggable(interface)) {
            fprintf(fp, "\n");
            fprintf(fp, "# This is a list of hotpluggable network interfaces.\n");
            fprintf(fp, "# They will be activated automatically by the "
                    "hotplug subsystem.\n");
            fprintf(fp, "mapping hotplug\n");
            fprintf(fp, "\tscript grep\n");
            fprintf(fp, "\tmap %s\n", interface);
        }
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
        if (ipaddress.s_addr) {
            fprintf(fp, "127.0.0.1\tlocalhost\t%s\n", hostname);
            if (domain && !empty_str(domain))
                fprintf(fp, "%s\t%s.%s\t%s\n",
                        inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)), hostname,
                        domain, hostname);
            else
                fprintf(fp, "%s\t%s\n", inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)),
                        hostname);
        } else {
            fprintf(fp, "127.0.0.1\tlocalhost\t%s\n", hostname);
        }

        fclose(fp);

        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       HOSTS_FILE, "/target" HOSTS_FILE);
    }
}

int netcfg_get_ipaddress(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    old_ipaddress = ipaddress;

    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_ipaddress", &ptr);
      if (ret)
	return ret;

      ok = inet_pton (AF_INET, ptr, &ipaddress);
      
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
    char *ptr;

    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_pointopoint", &ptr);
      if (ret)  
	return ret;

      if (empty_str(ptr)) /* No P-P is ok */
      {
	memset(&pointopoint, 0, sizeof(struct in_addr));
	return 0;
      }
      
      ok = inet_pton (AF_INET, ptr, &pointopoint);
      
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
    char *ptr, ptr1[INET_ADDRSTRLEN];
    struct in_addr old_netmask = netmask;
        
    while (!ok)
    {
      ret = my_debconf_input(client,"critical", "netcfg/get_netmask", &ptr);

      if (ret)
	return ret;

      ok = inet_pton (AF_INET, ptr, &netmask);
      
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

int netcfg_get_gateway(struct debconfclient *client)
{
    int ret, ok = 0;
    char *ptr;

    while (!ok)
    {
      ret = my_debconf_input(client, "critical", "netcfg/get_gateway", &ptr);
      if (ret)  
	return ret;

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
    
    ret = my_debconf_input(client, "high", "netcfg/get_nameservers", &ptr);
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

int kill_dhcp_client(void) {
    FILE *ps;
    char *pid_char = NULL;
    int pid_int = 0;
    size_t linesize = 0;
    ssize_t ret = 0;

    /* kill running dhcp client */

    if ((ps = popen("ps xa | grep 'udhcpc\\|dhclient\\|pump' | grep -v grep | sed 's/^ *//' | cut -d ' ' -f 1", "r")) == NULL)
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

    /* configure loopback */
    di_exec_shell_log("ifconfig lo 127.0.0.1");

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
    char *ptr, ptr1[INET_ADDRSTRLEN];
    char *none;

    enum { BACKUP, GET_IPADDRESS, GET_POINTOPOINT, GET_NETMASK, GET_GATEWAY, 
           GATEWAY_UNREACHABLE, GET_NAMESERVERS, CONFIRM, GET_DOMAIN, QUIT } state = GET_IPADDRESS;

    kill_dhcp_client();
	   
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


static void netcfg_write_dhcp (char* prebaseconfig, char *iface)
{
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "a"))) {
        fprintf(fp,
                "\n# This entry was created during the Debian installation\n");
        if (!iface_is_hotpluggable(iface))
            fprintf(fp, "auto %s\n", iface);
        fprintf(fp, "iface %s inet dhcp\n", iface);
	if (is_wireless_iface(iface))
	{
	  fprintf(fp, "\twireless_mode %s\n",
	      (mode == MANAGED) ? "managed" : "adhoc");
	  fprintf(fp, "\twireless_essid %s\n", essid ? essid : "any");
	  if (wepkey != NULL)
	    fprintf(fp, "\twireless_key %s\n", wepkey);
	}
        fclose(fp);
    }

    if ((fp = file_open(RESOLV_FILE, "a"))) {
      fclose(fp);
    }
    
    di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n", RESOLV_FILE,
	"/target" RESOLV_FILE);
}

#define DHCP_SECONDS 15

int netcfg_activate_dhcp(struct debconfclient *client)
{
    char buf[128];
    struct stat stat_buf;
    time_t start_time, now;
    pid_t pid = 0;
    int retry = 1;
    char *ptr;
    FILE *dc = NULL;

    enum { PUMP, DHCLIENT, DHCLIENT3, UDHCPC } dhcp_client;

    if (stat("/var/lib/dhcp3", &stat_buf) == 0)
        dhcp_client = DHCLIENT3;
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
    di_exec_shell_log("ifconfig lo 127.0.0.1");

    /* load kernel module for network sockets silently */
    di_exec_shell("modprobe af_packet");

    /* get dhcp lease */
    switch (dhcp_client) {
    case PUMP:
        snprintf(buf, sizeof(buf), "pump -i %s -h %s", interface, hostname);
        break;

    case DHCLIENT:
	/* First, set up dhclient.conf */
	if ((dc = file_open(DHCLIENT_CONF, "w")))
	{
	  fprintf(dc, "send host-name %s\n", hostname);
	  fclose(dc);
	}
        snprintf(buf, sizeof(buf), "dhclient -e %s", interface);
        break;

    case DHCLIENT3:
	/* Different place.. */
	if ((dc = file_open(DHCLIENT3_CONF, "w")))
	{
	  fprintf(dc, "send host-name %s\n", hostname);
	  fclose(dc);
	}
	snprintf(buf, sizeof(buf), "dhclient %s", interface);
	break;

    case UDHCPC:
        snprintf(buf, sizeof(buf), "udhcpc -i %s -n -H %s", hostname, interface);
        break;
    }

    while (retry == 1) {
        /* show progress bar */
        debconf_progress_start(client, 0, DHCP_SECONDS, "netcfg/dhcp_progress");
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
        while (dhcp_running && ((now - start_time) < DHCP_SECONDS)) {
            sleep(1);
            debconf_progress_step(client, 1);
            now = time(NULL);
        }

        /* stop progress bar */
        debconf_progress_stop(client);
        netcfg_progress_displayed = 0;

        /* got a lease? */
        if (!dhcp_running && (dhcp_exit_status == 0)) {
	    assert(hostname != NULL);

            /* write configuration */
            netcfg_write_common("40netcfg", ipaddress, hostname, domain);
            netcfg_write_dhcp("40netcfg", interface);

            return 0;
        }

        /* ask if user wants to retry */
        if (my_debconf_input(client, "high", "netcfg/dhcp_retry", &ptr) == 30) {
            if (dhcp_running) {
                kill(pid, SIGTERM);
            }
	    return 30; /* backup */
	}
        retry = strstr(ptr, "true") ? 1 : 0;
    }

    if (dhcp_running) {
        kill(pid, SIGTERM);
    }
    return 1;
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
  int ret, couldnt_associate = 0;
  wireless_config wconf;
  char* tf = NULL, *user_essid = NULL, *ptr = wconf.essid;

  if (wfd == 0) /* shouldn't happen */
    wfd = iw_sockets_open();

  iw_get_basic_config (wfd, iface, &wconf);

  debconf_subst(client, "netcfg/wireless_essid", "iface", iface);
  debconf_subst(client, "netcfg/wireless_adhoc_managed", "iface", iface);

  debconf_input(client, "low", "netcfg/wireless_adhoc_managed");

  if (debconf_go(client) == 30)
    return GO_BACK;

  debconf_get(client, "netcfg/wireless_adhoc_managed");

  if (!strcmp(client->value, "Ad-hoc network (Peer to peer)"))
    mode = ADHOC;

  wconf.has_mode = 1;
  wconf.mode = (mode == ADHOC) ? 1 : 2;

  debconf_input(client, "low", "netcfg/wireless_essid");

  if (debconf_go(client) == 30)
    return GO_BACK;

  debconf_get(client, "netcfg/wireless_essid");
  tf = strdup(client->value);

automatic:
  /* question not asked or user doesn't care or we're successfully associated */
  if (!empty_str(wconf.essid) || empty_str(client->value)) 
  {
    int i, success = 0;

    /* Default to any AP */
    wconf.essid[0] = '\0';
    wconf.essid_on = 0;

    iw_set_basic_config (wfd, iface, &wconf);

    /* Wait for association.. (MAX_SECS seconds)*/
#define MAX_SECS 3

    debconf_progress_start(client, 0, MAX_SECS, "netcfg/wifi_progress_title");
    debconf_progress_info(client, "netcfg/wifi_progress_info");

    for (i = 0; i <= MAX_SECS; i++)
    {
      ifconfig_up(iface);
      sleep (1);
      iw_get_basic_config (wfd, iface, &wconf);

      if (!empty_str(wconf.essid))
      {
	/* Save for later */
	debconf_set(client, "netcfg/wireless_essid", wconf.essid);
	debconf_progress_set(client, MAX_SECS);
	success = 1;
	break;
      }

      debconf_progress_step(client, 1);
      ifconfig_down(iface);
    }

    debconf_progress_stop(client);

    if (success)
      return 0;

    couldnt_associate = 1;
  }
  /* yes, wants to set an essid by himself */

  if (strlen(tf) <= IW_ESSID_MAX_SIZE) /* looks ok, let's use it */
    user_essid = tf;

  while (!user_essid || empty_str(user_essid) ||
      strlen(user_essid) > IW_ESSID_MAX_SIZE)
  {
    /* Misnomer of a check. Basically, if we went through autodetection,
     * we want to enter this loop, but we want to suppress anything that
     * relied on the checking of tf/user_essid (i.e. "", in most cases.) */
    if (!couldnt_associate)
    {
      debconf_subst(client, "netcfg/invalid_essid", "essid", user_essid);
      debconf_input(client, "high", "netcfg/invalid_essid");
      debconf_go(client);
    }

    ret = debconf_input(client,
	couldnt_associate ? "critical" : "low",
	"netcfg/wireless_essid");
   
    /* But now we'd not like to suppress any MORE errors */
    couldnt_associate = 0;
    
    /* we asked the question once, why can't we ask it again? */
    assert (ret != 30);

    if (debconf_go(client) == 30) /* well, we did, but he wants to go back */
      return GO_BACK;

    debconf_get(client, "netcfg/wireless_essid");

    if (empty_str(client->value))
      goto automatic;

    free(user_essid);
    user_essid = strdup(client->value);
  }

  essid = user_essid;

  memset(ptr, 0, IW_ESSID_MAX_SIZE + 1);
  snprintf(wconf.essid, IW_ESSID_MAX_SIZE + 1, "%s", essid);
  wconf.has_essid = 1;
  wconf.essid_on = 1;

  iw_set_basic_config (wfd, iface, &wconf);

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
    return GO_BACK;

  if (empty_str(rv))
  {
    unset_wep_key (iface);

    if (wepkey != NULL)
    {
      free(wepkey);
      wepkey = NULL;
    }
    
    return 0;
  }

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

/* Utility functions. */
int ifconfig_up (char* iface)
{
  char * cmd;
  size_t len = sizeof("ifconfig  up") + strlen(iface) + 1;
  int ret;

  if (!(cmd = malloc(len)))
		return 1;

  snprintf(cmd, len, "ifconfig %s up", iface);

  ret = di_exec_shell(cmd);

  free(cmd);

	return ret;
}

int ifconfig_down (char* iface)
{
  char * cmd;
  size_t len = sizeof("ifconfig  down") + strlen(iface) + 1;
  int ret;

  if (!(cmd = malloc(len)))
		return 1;

  snprintf(cmd, len, "ifconfig %s down", iface);

  ret = di_exec_shell(cmd);

  free(cmd);

	return ret;
}
